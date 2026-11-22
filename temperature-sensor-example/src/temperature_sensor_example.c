#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>
#if CONFIG_KFSW_EVENT
#include <kfsw/services/event.h>
#endif
#include <kfsw/services/log.h>

#include "temperature_sensor_example_internal.h"

/*
 * Reading the ADC takes a driver mutex and sample callbacks run under the table
 * lock, so the module polls on its own work queue and the table copies the
 * cached value.
 */

static struct kfsw_temp_example_reading cached = {
	.milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C,
};
static K_MUTEX_DEFINE(cache_lock);
static bool initialized;
static uint64_t last_success_ms;
static int32_t alarm_milli_c = CONFIG_KFSW_TEMP_EXAMPLE_ALARM_MILLI_C;

static void report_alarm(bool raised, int32_t milli_c)
{
#if CONFIG_KFSW_EVENT
	uint8_t payload[sizeof(int32_t)];

	sys_put_be32((uint32_t)milli_c, payload);
	kfsw_event_emit(KFSW_EVENT_SOURCE_APP,
			raised ? KFSW_EVENT_TEMP_EXAMPLE_ALARM
			       : KFSW_EVENT_TEMP_EXAMPLE_ALARM_CLEARED,
			raised ? KFSW_EVENT_WARNING : KFSW_EVENT_INFO, payload, sizeof(payload));
#endif
	if (raised) {
		kfsw_log_warning("Temperature %d mC reached the alarm threshold", milli_c);
	} else {
		kfsw_log_info("Temperature %d mC is back below the alarm threshold", milli_c);
	}
}

void kfsw_temp_example_store(int32_t milli_c, uint64_t monotonic_ms)
{
	bool was_alarm;
	bool is_alarm;

	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	cached.milli_c = milli_c;
	cached.last_uptime_ms = (uint32_t)monotonic_ms;
	last_success_ms = monotonic_ms;
	cached.valid = true;
	if (cached.samples < UINT32_MAX) {
		cached.samples++;
	}
	was_alarm = cached.alarm;
	is_alarm = milli_c >= alarm_milli_c;
	cached.alarm = is_alarm;
	if (is_alarm && !was_alarm && (cached.alarms < UINT32_MAX)) {
		cached.alarms++;
	}
	k_mutex_unlock(&cache_lock);

	/* Outside the lock: the event ring and the log take their own. */
	if (is_alarm != was_alarm) {
		report_alarm(is_alarm, milli_c);
	}
}

void kfsw_temp_example_store_failure(void)
{
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	/* Drop the last good reading when a read fails. */
	cached.milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C;
	cached.valid = false;
	if (cached.failures < UINT32_MAX) {
		cached.failures++;
	}
	k_mutex_unlock(&cache_lock);
}

int kfsw_temp_example_set_alarm(int32_t milli_c)
{
	if ((milli_c < KFSW_TEMP_EXAMPLE_ALARM_MIN_MILLI_C) ||
	    (milli_c > KFSW_TEMP_EXAMPLE_ALARM_MAX_MILLI_C)) {
		return -ERANGE;
	}
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	alarm_milli_c = milli_c;
	k_mutex_unlock(&cache_lock);
	return 0;
}

int kfsw_temp_example_clear(void)
{
	int result = 0;

	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	if (!initialized) {
		result = -EACCES;
	} else {
		cached.samples = 0U;
		cached.failures = 0U;
		cached.alarms = 0U;
	}
	k_mutex_unlock(&cache_lock);
	return result;
}

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
static struct k_work_q sensor_queue;
static K_THREAD_STACK_DEFINE(sensor_stack, CONFIG_KFSW_TEMP_EXAMPLE_STACK_SIZE);
static void poll_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_work_handler);

static int sample_once(void)
{
	static bool failing;
	int32_t milli_c;
	int result;

	result = kfsw_temp_example_sensor_read(&milli_c);
	if (result != 0) {
		kfsw_temp_example_store_failure();
		/* Once per outage, not once per period. */
		if (!failing) {
			kfsw_log_warning("Temperature read failed: %d", result);
		}
		failing = true;
		return result;
	}
	if (failing) {
		kfsw_log_info("Temperature readings are back");
	}
	failing = false;
	kfsw_temp_example_store(milli_c, kfsw_time_monotonic_ms());
	return 0;
}

static void poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)sample_once();
	(void)k_work_reschedule_for_queue(&sensor_queue, &poll_work,
					  K_MSEC(CONFIG_KFSW_TEMP_EXAMPLE_PERIOD_MS));
}
#endif

int kfsw_temp_example_init(void)
{
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	if (initialized) {
		k_mutex_unlock(&cache_lock);
		return 0;
	}

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
	int result = kfsw_temp_example_sensor_prepare();
	if (result != 0) {
		k_mutex_unlock(&cache_lock);
		kfsw_log_error("Temperature sensor not ready: %d", result);
		return result;
	}
	k_work_queue_start(&sensor_queue, sensor_stack, K_THREAD_STACK_SIZEOF(sensor_stack),
			   CONFIG_KFSW_TEMP_EXAMPLE_PRIORITY, NULL);
	(void)k_thread_name_set(&sensor_queue.thread, "kfsw_temp");
	(void)k_work_reschedule_for_queue(&sensor_queue, &poll_work, K_NO_WAIT);
	kfsw_log_info("Temperature sampled every %d ms", CONFIG_KFSW_TEMP_EXAMPLE_PERIOD_MS);
#endif
	initialized = true;
	k_mutex_unlock(&cache_lock);
	return 0;
}

int kfsw_temp_example_get(struct kfsw_temp_example_reading *reading)
{
	if (reading == NULL) {
		return -EINVAL;
	}
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	if (!initialized) {
		k_mutex_unlock(&cache_lock);
		return -EACCES;
	}
	*reading = cached;
	uint64_t now = kfsw_time_monotonic_ms();

	if (reading->valid && ((now < last_success_ms) ||
			       (now - last_success_ms > CONFIG_KFSW_TEMP_EXAMPLE_MAX_AGE_MS))) {
		reading->valid = false;
		reading->milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C;
	}
	k_mutex_unlock(&cache_lock);
	return 0;
}
