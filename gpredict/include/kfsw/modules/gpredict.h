#ifndef KFSW_MODULES_GPREDICT_H
#define KFSW_MODULES_GPREDICT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup kfsw_modules_gpredict Pass tracking
 * @ingroup kfsw_modules
 *
 * Following a spacecraft across a pass, driven by a predictor on the ground.
 *
 * Gpredict computes the pass and speaks the hamlib protocols: it sends a
 * bearing to a rotator daemon and a corrected frequency to a radio daemon.
 * This module is the other end of those two conversations, so prediction stays
 * where it already works and this node only has to point and tune.
 *
 * Every question worth getting right here is about what happens when the
 * conversation stops. A rotator left at the last bearing it was given points
 * at nothing; one that returns to its park position in the middle of a pass is
 * worse, because the pass is lost and nobody is told. So the decision is a
 * state machine with a grace period: a gap shorter than the grace is a gap in
 * the predictor, and the antenna holds; a gap longer than it is the end of the
 * pass, and the antenna parks.
 *
 * @{
 */

/** Where the tracker is. */
enum kfsw_gpredict_state {
	/** Nothing has ever been tracked, or the last pass finished and parked. */
	KFSW_GPREDICT_PARKED = 0,
	/** Bearings are arriving and the antenna is following them. */
	KFSW_GPREDICT_TRACKING = 1,
	/**
	 * Bearings stopped arriving but not for long enough to call the pass
	 * over. The antenna holds its last bearing, because a predictor that
	 * stalls for a second is not a pass that ended.
	 */
	KFSW_GPREDICT_HOLDING = 2,
	/** On the way to the park position, after the pass or on command. */
	KFSW_GPREDICT_PARKING = 3,
	/**
	 * The rotator reported a problem, or a bearing was refused. Tracking
	 * does not resume on its own: an operator has to clear it, because a
	 * fault that clears itself hides a rotator that is failing.
	 */
	KFSW_GPREDICT_FAULT = 4,
};

/** What happened. The state machine is driven only by these. */
enum kfsw_gpredict_event {
	/** A bearing arrived from the predictor. */
	KFSW_GPREDICT_EVENT_BEARING = 0,
	/** The grace period expired with no bearing. */
	KFSW_GPREDICT_EVENT_SILENCE = 1,
	/** The rotator reported it reached the park position. */
	KFSW_GPREDICT_EVENT_PARKED = 2,
	/** An operator asked for the park position now. */
	KFSW_GPREDICT_EVENT_PARK_COMMANDED = 3,
	/** The rotator or the link to it failed. */
	KFSW_GPREDICT_EVENT_FAULT = 4,
	/** An operator cleared a fault. */
	KFSW_GPREDICT_EVENT_CLEARED = 5,
};

/** A bearing, as the predictor gives it. */
struct kfsw_gpredict_bearing {
	/** Degrees, 0 to 360. */
	int32_t azimuth_mdeg;
	/** Degrees, -90 to 90. Below the horizon is refused. */
	int32_t elevation_mdeg;
};

/** What an operator can see without asking the rotator. */
struct kfsw_gpredict_status {
	/** enum kfsw_gpredict_state */
	uint8_t state;
	/** Bearing last accepted, in millidegrees. */
	int32_t azimuth_mdeg;
	int32_t elevation_mdeg;
	/** Bearing the rotator last reported, in millidegrees. */
	int32_t actual_azimuth_mdeg;
	int32_t actual_elevation_mdeg;
	/** Frequency last accepted from the predictor, in hertz. 0 if none. */
	uint64_t frequency_hz;
	/** Milliseconds since the last bearing, 0 while none has arrived. */
	uint32_t silence_ms;
	/** Grace period in force, in milliseconds. */
	uint32_t grace_ms;
	/** Bearings accepted since boot. */
	uint32_t bearings;
	/** Bearings refused since boot, below the horizon or out of range. */
	uint32_t refusals;
	/** Passes that ended because the predictor went quiet. */
	uint32_t passes_ended;
	/** Faults recorded since boot. */
	uint32_t faults;
	/** Why the last refusal or fault happened, or 0. */
	int last_error;
};

/**
 * @brief Decide the next state.
 *
 * Pure: the same state and event always give the same answer, which is what
 * makes the end of a pass testable without a rotator.
 *
 * @param state Where the tracker is.
 * @param event What happened.
 * @return The state to move to, which may be @p state.
 */
enum kfsw_gpredict_state kfsw_gpredict_next(enum kfsw_gpredict_state state,
					    enum kfsw_gpredict_event event);

/** Human-readable name of a state, for the shell and for a log line. */
const char *kfsw_gpredict_state_name(enum kfsw_gpredict_state state);

/** Start the tracker parked, with the compiled grace period. */
int kfsw_gpredict_init(void);

/**
 * @brief Offer a bearing from the predictor.
 *
 * @retval 0 Accepted; the antenna is tracking.
 * @retval -EINVAL A NULL, or a bearing outside the rotator's range.
 * @retval -EPERM The tracker is in fault and has not been cleared.
 */
int kfsw_gpredict_bearing(const struct kfsw_gpredict_bearing *bearing);

/** Offer a Doppler-corrected frequency. Zero is refused with -EINVAL. */
int kfsw_gpredict_frequency(uint64_t frequency_hz);

/** Ask for the park position now, whatever the pass is doing. */
int kfsw_gpredict_park(void);

/** Clear a fault. Returns -EALREADY when there is nothing to clear. */
int kfsw_gpredict_clear(void);

/** Read the tracker. Returns -EINVAL for a NULL destination. */
int kfsw_gpredict_get_status(struct kfsw_gpredict_status *status);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* KFSW_MODULES_GPREDICT_H */
