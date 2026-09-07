#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>

#include "temperature_sensor_example_internal.h"

/*
 * A sensor read is an ADC conversion behind a driver mutex, and parameter
 * sample callbacks run under the table lock. So the module polls on its own
 * schedule and the table only ever copies what is already here.
 */

static struct kfsw_temp_example_reading cached = {
	.milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C,
};
static struct k_mutex cache_lock;
static bool initialized;

void kfsw_temp_example_store(int32_t milli_c, uint32_t monotonic_ms)
{
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	cached.milli_c = milli_c;
	cached.last_uptime_ms = monotonic_ms;
	cached.valid = true;
	if (cached.samples < UINT32_MAX) {
		cached.samples++;
	}
	k_mutex_unlock(&cache_lock);
}

void kfsw_temp_example_store_failure(void)
{
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	/* The last good reading is dropped rather than left standing: a stale
	 * number that keeps being served reads as a working sensor.
	 */
	cached.milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C;
	cached.valid = false;
	if (cached.failures < UINT32_MAX) {
		cached.failures++;
	}
	k_mutex_unlock(&cache_lock);
}

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
static void poll_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_work_handler);

static int sample_once(void)
{
	int32_t milli_c;
	int result;

	result = kfsw_temp_example_sensor_read(&milli_c);
	if (result != 0) {
		kfsw_temp_example_store_failure();
		return result;
	}
	kfsw_temp_example_store(milli_c, (uint32_t)kfsw_time_monotonic_ms());
	return 0;
}

static void poll_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)sample_once();
	(void)k_work_reschedule(&poll_work, K_MSEC(CONFIG_KFSW_TEMP_EXAMPLE_PERIOD_MS));
}
#endif

int kfsw_temp_example_init(void)
{
	int result = 0;

	if (initialized) {
		return 0;
	}
	(void)k_mutex_init(&cache_lock);
	initialized = true;

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
	result = kfsw_temp_example_sensor_prepare();
	if (result != 0) {
		initialized = false;
		return result;
	}
	/* Read once here so a successful init means the sensor answered, and
	 * the first housekeeping collection does not have to wait a period.
	 */
	result = sample_once();
	(void)k_work_reschedule(&poll_work, K_MSEC(CONFIG_KFSW_TEMP_EXAMPLE_PERIOD_MS));
#endif
	return result;
}

int kfsw_temp_example_get(struct kfsw_temp_example_reading *reading)
{
	if (reading == NULL) {
		return -EINVAL;
	}
	if (!initialized) {
		return -EACCES;
	}
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	*reading = cached;
	k_mutex_unlock(&cache_lock);
	return 0;
}
