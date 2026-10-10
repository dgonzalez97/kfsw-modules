#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>
#include <kfsw/services/log.h>

#include "gpredict_internal.h"

/*
 * The travel the rotator is allowed, in millidegrees. Bearings outside it are
 * refused rather than clamped: a clamped bearing points at the wrong sky and
 * reports success while doing it.
 */
#define GPREDICT_AZIMUTH_MIN_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG * 1000)
#define GPREDICT_AZIMUTH_MAX_MDEG (CONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG * 1000)
#define GPREDICT_ELEVATION_MIN_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG * 1000)
#define GPREDICT_ELEVATION_MAX_MDEG (CONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG * 1000)
#define GPREDICT_PARK_AZIMUTH_MDEG (CONFIG_KFSW_GPREDICT_PARK_AZIMUTH_DEG * 1000)
#define GPREDICT_PARK_ELEVATION_MDEG (CONFIG_KFSW_GPREDICT_PARK_ELEVATION_DEG * 1000)

BUILD_ASSERT(CONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG < CONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG,
	     "kfsw,gpredict azimuth travel must have a minimum below its maximum");
BUILD_ASSERT(CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG < CONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG,
	     "kfsw,gpredict elevation travel must have a minimum below its maximum");
BUILD_ASSERT(CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG >= 0,
	     "a bearing below the horizon is refused, so the travel cannot reach below it");
BUILD_ASSERT((CONFIG_KFSW_GPREDICT_PARK_AZIMUTH_DEG >= CONFIG_KFSW_GPREDICT_AZIMUTH_MIN_DEG) &&
		     (CONFIG_KFSW_GPREDICT_PARK_AZIMUTH_DEG <=
		      CONFIG_KFSW_GPREDICT_AZIMUTH_MAX_DEG),
	     "the park azimuth must be inside the azimuth travel");
BUILD_ASSERT((CONFIG_KFSW_GPREDICT_PARK_ELEVATION_DEG >= CONFIG_KFSW_GPREDICT_ELEVATION_MIN_DEG) &&
		     (CONFIG_KFSW_GPREDICT_PARK_ELEVATION_DEG <=
		      CONFIG_KFSW_GPREDICT_ELEVATION_MAX_DEG),
	     "the park elevation must be inside the elevation travel");
BUILD_ASSERT((CONFIG_KFSW_GPREDICT_GRACE_MS >= KFSW_GPREDICT_GRACE_MIN_MS) &&
		     (CONFIG_KFSW_GPREDICT_GRACE_MS <= KFSW_GPREDICT_GRACE_MAX_MS),
	     "the compiled grace period must be one an operator could also set");

/*
 * One tracker per node. The lock covers the status and the last bearing time.
 * Parameter sample and change callbacks run under the table lock and call in
 * here, and nothing in here calls the parameter service, so the order is always
 * table lock then tracker lock and cannot invert.
 */
static struct kfsw_gpredict_status tracker = {
	.state = KFSW_GPREDICT_PARKED,
	.grace_ms = CONFIG_KFSW_GPREDICT_GRACE_MS,
};
static K_MUTEX_DEFINE(tracker_lock);
static uint64_t last_bearing_ms;
static bool bearing_seen;
static bool initialized;
static bool refusing;

static void silence_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(silence_work, silence_work_handler);

/* Lifetime counters saturate; one that wraps hides what it was counting. */
static void count_up(uint32_t *counter)
{
	/* Saturate: a wrapped counter reads as a quiet pass. */
	if (*counter < UINT32_MAX) {
		(*counter)++;
	}
}

enum kfsw_gpredict_state kfsw_gpredict_next(enum kfsw_gpredict_state state,
					    enum kfsw_gpredict_event event)
{
	switch (state) {
	case KFSW_GPREDICT_PARKED:
		switch (event) {
		case KFSW_GPREDICT_EVENT_BEARING:
			return KFSW_GPREDICT_TRACKING;
		case KFSW_GPREDICT_EVENT_SILENCE:
		case KFSW_GPREDICT_EVENT_PARKED:
		case KFSW_GPREDICT_EVENT_CLEARED:
			return KFSW_GPREDICT_PARKED;
		case KFSW_GPREDICT_EVENT_PARK_COMMANDED:
			return KFSW_GPREDICT_PARKING;
		case KFSW_GPREDICT_EVENT_FAULT:
			return KFSW_GPREDICT_FAULT;
		}
		break;
	case KFSW_GPREDICT_TRACKING:
		switch (event) {
		case KFSW_GPREDICT_EVENT_BEARING:
		case KFSW_GPREDICT_EVENT_PARKED:
		case KFSW_GPREDICT_EVENT_CLEARED:
			return KFSW_GPREDICT_TRACKING;
		case KFSW_GPREDICT_EVENT_SILENCE:
			return KFSW_GPREDICT_HOLDING;
		case KFSW_GPREDICT_EVENT_PARK_COMMANDED:
			return KFSW_GPREDICT_PARKING;
		case KFSW_GPREDICT_EVENT_FAULT:
			return KFSW_GPREDICT_FAULT;
		}
		break;
	case KFSW_GPREDICT_HOLDING:
		switch (event) {
		case KFSW_GPREDICT_EVENT_BEARING:
			return KFSW_GPREDICT_TRACKING;
		case KFSW_GPREDICT_EVENT_SILENCE:
		case KFSW_GPREDICT_EVENT_PARK_COMMANDED:
			return KFSW_GPREDICT_PARKING;
		case KFSW_GPREDICT_EVENT_PARKED:
		case KFSW_GPREDICT_EVENT_CLEARED:
			return KFSW_GPREDICT_HOLDING;
		case KFSW_GPREDICT_EVENT_FAULT:
			return KFSW_GPREDICT_FAULT;
		}
		break;
	case KFSW_GPREDICT_PARKING:
		switch (event) {
		/*
		 * A pass does not resume halfway into a park. The park finishes
		 * and the bearing after that starts the next pass.
		 */
		case KFSW_GPREDICT_EVENT_BEARING:
		case KFSW_GPREDICT_EVENT_SILENCE:
		case KFSW_GPREDICT_EVENT_PARK_COMMANDED:
		case KFSW_GPREDICT_EVENT_CLEARED:
			return KFSW_GPREDICT_PARKING;
		case KFSW_GPREDICT_EVENT_PARKED:
			return KFSW_GPREDICT_PARKED;
		case KFSW_GPREDICT_EVENT_FAULT:
			return KFSW_GPREDICT_FAULT;
		}
		break;
	case KFSW_GPREDICT_FAULT:
		switch (event) {
		case KFSW_GPREDICT_EVENT_CLEARED:
			return KFSW_GPREDICT_PARKED;
		/* An operator clears a fault. Nothing else leaves it. */
		case KFSW_GPREDICT_EVENT_BEARING:
		case KFSW_GPREDICT_EVENT_SILENCE:
		case KFSW_GPREDICT_EVENT_PARKED:
		case KFSW_GPREDICT_EVENT_PARK_COMMANDED:
		case KFSW_GPREDICT_EVENT_FAULT:
			return KFSW_GPREDICT_FAULT;
		}
		break;
	}

	/* Not a state and event pair this module defines: contain it. */
	return KFSW_GPREDICT_FAULT;
}

const char *kfsw_gpredict_state_name(enum kfsw_gpredict_state state)
{
	switch (state) {
	case KFSW_GPREDICT_PARKED:
		return "parked";
	case KFSW_GPREDICT_TRACKING:
		return "tracking";
	case KFSW_GPREDICT_HOLDING:
		return "holding";
	case KFSW_GPREDICT_PARKING:
		return "parking";
	case KFSW_GPREDICT_FAULT:
		return "fault";
	}

	return "unknown";
}

static bool pass_is_live(void)
{
	return (tracker.state == KFSW_GPREDICT_TRACKING) ||
	       (tracker.state == KFSW_GPREDICT_HOLDING);
}

static uint32_t silence_now(void)
{
	uint64_t now;
	uint64_t elapsed;

	if (!bearing_seen) {
		return 0U;
	}

	now = kfsw_time_monotonic_ms();
	/* A clock that went backwards reports no silence rather than an age. */
	elapsed = (now > last_bearing_ms) ? (now - last_bearing_ms) : 0U;
	return (elapsed > UINT32_MAX) ? UINT32_MAX : (uint32_t)elapsed;
}

static void apply_event(enum kfsw_gpredict_event event)
{
	const enum kfsw_gpredict_state previous = (enum kfsw_gpredict_state)tracker.state;
	const enum kfsw_gpredict_state next = kfsw_gpredict_next(previous, event);

	if (event == KFSW_GPREDICT_EVENT_PARKED) {
		/* The one report that carries its own bearing: park is a position. */
		tracker.actual_azimuth_mdeg = GPREDICT_PARK_AZIMUTH_MDEG;
		tracker.actual_elevation_mdeg = GPREDICT_PARK_ELEVATION_MDEG;
	}

	if (next == previous) {
		return;
	}

	tracker.state = (uint8_t)next;
	if (next == KFSW_GPREDICT_FAULT) {
		count_up(&tracker.faults);
		tracker.last_error = -EIO;
	}
	if ((previous == KFSW_GPREDICT_HOLDING) && (event == KFSW_GPREDICT_EVENT_SILENCE)) {
		count_up(&tracker.passes_ended);
	}

	kfsw_log_info("Pass tracking %s -> %s", kfsw_gpredict_state_name(previous),
		      kfsw_gpredict_state_name(next));
}

void gpredict_apply_event(enum kfsw_gpredict_event event)
{
	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	apply_event(event);
	k_mutex_unlock(&tracker_lock);
}

static void reset(void)
{
	(void)k_work_cancel_delayable(&silence_work);
	tracker = (struct kfsw_gpredict_status){
		.state = KFSW_GPREDICT_PARKED,
		.grace_ms = CONFIG_KFSW_GPREDICT_GRACE_MS,
	};
	last_bearing_ms = 0U;
	bearing_seen = false;
	refusing = false;
	initialized = true;
}

void gpredict_state_reset(void)
{
	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	reset();
	k_mutex_unlock(&tracker_lock);
}

int kfsw_gpredict_init(void)
{
	bool first;

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	first = !initialized;
	if (first) {
		reset();
	}
	k_mutex_unlock(&tracker_lock);

	if (first) {
		kfsw_log_info("Pass tracking parked, %u ms grace",
			      (unsigned int)CONFIG_KFSW_GPREDICT_GRACE_MS);
	}
	return 0;
}

static bool within_travel(const struct kfsw_gpredict_bearing *bearing)
{
	return (bearing->azimuth_mdeg >= GPREDICT_AZIMUTH_MIN_MDEG) &&
	       (bearing->azimuth_mdeg <= GPREDICT_AZIMUTH_MAX_MDEG) &&
	       (bearing->elevation_mdeg >= GPREDICT_ELEVATION_MIN_MDEG) &&
	       (bearing->elevation_mdeg <= GPREDICT_ELEVATION_MAX_MDEG);
}

int kfsw_gpredict_bearing(const struct kfsw_gpredict_bearing *bearing)
{
	int result = 0;

	if (bearing == NULL) {
		/* Not counted: refusals are bearings the predictor sent that this
		 * rotator cannot reach, and a NULL never came off the wire.
		 */
		return -EINVAL;
	}

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	if (tracker.state == KFSW_GPREDICT_FAULT) {
		result = -EPERM;
	} else if (!within_travel(bearing)) {
		result = -EINVAL;
	}

	if (result != 0) {
		count_up(&tracker.refusals);
		tracker.last_error = result;
		if (!refusing) {
			/* Once per run of refusals: a stuck predictor sends many. */
			kfsw_log_warning("Pass tracking refused az %d el %d mdeg: %d",
					 bearing->azimuth_mdeg, bearing->elevation_mdeg, result);
		}
		refusing = true;
		k_mutex_unlock(&tracker_lock);
		return result;
	}

	if (refusing) {
		kfsw_log_info("Pass tracking is taking bearings again");
	}
	refusing = false;
	tracker.azimuth_mdeg = bearing->azimuth_mdeg;
	tracker.elevation_mdeg = bearing->elevation_mdeg;
	last_bearing_ms = kfsw_time_monotonic_ms();
	bearing_seen = true;
	count_up(&tracker.bearings);
	apply_event(KFSW_GPREDICT_EVENT_BEARING);
	(void)k_work_reschedule(&silence_work, K_MSEC(tracker.grace_ms));
	k_mutex_unlock(&tracker_lock);
	return 0;
}

int kfsw_gpredict_frequency(uint64_t frequency_hz)
{
	int result = 0;

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	if (frequency_hz == 0U) {
		/* Zero is what an unparsed or absent frequency looks like. */
		result = -EINVAL;
		tracker.last_error = result;
	} else {
		tracker.frequency_hz = frequency_hz;
	}
	k_mutex_unlock(&tracker_lock);
	return result;
}

int kfsw_gpredict_park(void)
{
	int result = 0;

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	if (tracker.state == KFSW_GPREDICT_FAULT) {
		/* The rotator cannot be trusted to move, so a park would be a
		 * claim nothing confirmed. An operator clears the fault first.
		 */
		result = -EPERM;
		tracker.last_error = result;
	} else {
		apply_event(KFSW_GPREDICT_EVENT_PARK_COMMANDED);
	}
	k_mutex_unlock(&tracker_lock);
	return result;
}

int kfsw_gpredict_clear(void)
{
	int result = 0;

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	if (tracker.state != KFSW_GPREDICT_FAULT) {
		result = -EALREADY;
	} else {
		apply_event(KFSW_GPREDICT_EVENT_CLEARED);
	}
	k_mutex_unlock(&tracker_lock);
	return result;
}

int kfsw_gpredict_get_status(struct kfsw_gpredict_status *status)
{
	if (status == NULL) {
		return -EINVAL;
	}

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	tracker.silence_ms = silence_now();
	*status = tracker;
	k_mutex_unlock(&tracker_lock);
	return 0;
}

int gpredict_grace_validate(uint32_t grace_ms)
{
	if ((grace_ms < KFSW_GPREDICT_GRACE_MIN_MS) || (grace_ms > KFSW_GPREDICT_GRACE_MAX_MS)) {
		return -ERANGE;
	}
	return 0;
}

void gpredict_grace_set(uint32_t grace_ms)
{
	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	tracker.grace_ms = grace_ms;
	if (pass_is_live()) {
		/* A shortened grace must not have to wait out the old one. */
		(void)k_work_reschedule(&silence_work, K_MSEC(grace_ms));
	}
	k_mutex_unlock(&tracker_lock);
}

void gpredict_silence_check(void)
{
	uint32_t waited;

	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	waited = silence_now();
	if (pass_is_live() && (waited >= tracker.grace_ms)) {
		apply_event(KFSW_GPREDICT_EVENT_SILENCE);
	}
	if (pass_is_live()) {
		/*
		 * Wake when the grace period is actually up, not a whole one
		 * later. Waking early by a tick and then waiting again would
		 * take two periods to notice a pass that ended.
		 */
		(void)k_work_reschedule(&silence_work, K_MSEC((waited < tracker.grace_ms)
								      ? (tracker.grace_ms - waited)
								      : tracker.grace_ms));
	}
	k_mutex_unlock(&tracker_lock);
}

static void silence_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	gpredict_silence_check();
}

#if CONFIG_ZTEST
void gpredict_test_set_counters(uint32_t value)
{
	(void)k_mutex_lock(&tracker_lock, K_FOREVER);
	tracker.bearings = value;
	tracker.refusals = value;
	tracker.passes_ended = value;
	tracker.faults = value;
	k_mutex_unlock(&tracker_lock);
}
#endif
