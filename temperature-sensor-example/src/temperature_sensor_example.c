#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#if CONFIG_KFSW_TEMP_EXAMPLE_SHELL
#include <zephyr/shell/shell.h>
#endif

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
static K_MUTEX_DEFINE(cache_lock);
static bool initialized;
static uint64_t last_success_ms;

void kfsw_temp_example_store(int32_t milli_c, uint64_t monotonic_ms)
{
	(void)k_mutex_lock(&cache_lock, K_FOREVER);
	cached.milli_c = milli_c;
	cached.last_uptime_ms = (uint32_t)monotonic_ms;
	last_success_ms = monotonic_ms;
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
static struct k_work_q sensor_queue;
static K_THREAD_STACK_DEFINE(sensor_stack, CONFIG_KFSW_TEMP_EXAMPLE_STACK_SIZE);
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
	k_mutex_lock(&cache_lock, K_FOREVER);
	if (initialized) {
		k_mutex_unlock(&cache_lock);
		return 0;
	}

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
	int result = kfsw_temp_example_sensor_prepare();
	if (result != 0) {
		k_mutex_unlock(&cache_lock);
		return result;
	}
	k_work_queue_start(&sensor_queue, sensor_stack, K_THREAD_STACK_SIZEOF(sensor_stack),
			   CONFIG_KFSW_TEMP_EXAMPLE_PRIORITY, NULL);
	(void)k_thread_name_set(&sensor_queue.thread, "kfsw_temp");
	(void)k_work_reschedule_for_queue(&sensor_queue, &poll_work, K_NO_WAIT);
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
	k_mutex_lock(&cache_lock, K_FOREVER);
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

#if CONFIG_KFSW_TEMP_EXAMPLE_SHELL
static int cmd_temp_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_temp_example_reading reading;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_temp_example_get(&reading);
	if (result != 0) {
		shell_error(sh, "temperature unavailable (%d)", result);
		return result;
	}

	if (reading.valid) {
		/* Degrees and thousandths rather than a float, so a shell command
		 * does not pull in soft-float printing. The sign is carried
		 * separately because -0.5 C truncates to a whole part of 0.
		 */
		int32_t magnitude = (reading.milli_c < 0) ? -reading.milli_c : reading.milli_c;

		shell_print(sh, "die: %s%d.%03d C", (reading.milli_c < 0) ? "-" : "",
			    magnitude / 1000, magnitude % 1000);
	} else {
		shell_print(sh, "die: absent");
	}
	shell_print(sh, "samples: %u", reading.samples);
	shell_print(sh, "failures: %u", reading.failures);
	shell_print(sh, "last read at: %u ms", reading.last_uptime_ms);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(temp_commands,
	SHELL_CMD_ARG(status, NULL, "Show the cached die temperature.", cmd_temp_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(temp, &temp_commands, "Temperature example: the MCU die reading.", NULL);
#endif
