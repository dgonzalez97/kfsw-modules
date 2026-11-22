#ifndef KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_H
#define KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kfsw_param_definition_set;
struct kfsw_command_definition_set;

/** Parameter table reserved for the temperature example, in the module band. */
#define KFSW_TEMP_EXAMPLE_PARAM_TABLE_ID 51U
/** Parameter table name. */
#define KFSW_TEMP_EXAMPLE_PARAM_TABLE_NAME "temp_example"

/** Wire ID of temp_clear, which zeroes the counters. Never reused. */
#define KFSW_COMMAND_ID_TEMP_EXAMPLE_CLEAR 64U

/**
 * Event IDs of this module, emitted under KFSW_EVENT_SOURCE_APP until modules
 * have a source band. They start at 64 so they cannot meet the app's own.
 * Payload: the reading in thousandths of a degree, big-endian i32.
 */
enum kfsw_temp_example_event {
	/** A reading rose to or above the alarm threshold. */
	KFSW_EVENT_TEMP_EXAMPLE_ALARM = 64,
	/** A reading fell back below the alarm threshold. */
	KFSW_EVENT_TEMP_EXAMPLE_ALARM_CLEARED = 65,
};

/**
 * Sentinel for an invalid or unavailable temperature.
 */
#define KFSW_TEMP_EXAMPLE_INVALID_MILLI_C INT32_MIN

/** @defgroup kfsw_modules_temp_example Temperature sensor example
 *  @ingroup kfsw_modules
 *  A sensor read into a parameter table, for housekeeping.
 *
 *  @{
 */

/** Cached temperature and read counters. */
struct kfsw_temp_example_reading {
	/** Latest temperature in thousandths of a degree Celsius. */
	int32_t milli_c;
	/** Successful reads. */
	uint32_t samples;
	/** Reads that returned an error. */
	uint32_t failures;
	/** Low 32 bits of monotonic milliseconds at the latest successful read. */
	uint32_t last_uptime_ms;
	/** Times a reading crossed the alarm threshold upwards. */
	uint32_t alarms;
	/** Whether milli_c is a successful reading within the maximum age. */
	bool valid;
	/** Whether the latest reading is at or above the alarm threshold. */
	bool alarm;
};

/**
 * Initialize the cache and start the configured sensor.
 *
 * Queues the first reading on the module's workqueue. The cache stays invalid
 * until that read completes; initialization does not wait for a conversion.
 *
 * @return 0 when initialized, or a negative errno if the sensor cannot be bound.
 */
int kfsw_temp_example_init(void);

/**
 * Copy a consistent snapshot of the cached reading.
 *
 * Doesn't touch the ADC, so it can be called from a parameter sample callback.
 * Readings older than CONFIG_KFSW_TEMP_EXAMPLE_MAX_AGE_MS are returned as invalid.
 *
 * @param reading Destination.
 * @return 0 on success, -EINVAL for a NULL destination, or -EACCES before
 *         initialization.
 */
int kfsw_temp_example_get(struct kfsw_temp_example_reading *reading);

/**
 * Set the alarm threshold. Takes effect at the next reading.
 *
 * @param milli_c Threshold in thousandths of a degree Celsius.
 * @return 0, or -ERANGE outside KFSW_TEMP_EXAMPLE_ALARM_MIN_MILLI_C and
 *         KFSW_TEMP_EXAMPLE_ALARM_MAX_MILLI_C.
 */
int kfsw_temp_example_set_alarm(int32_t milli_c);

/** Lowest and highest accepted alarm threshold, the die's rated range. */
#define KFSW_TEMP_EXAMPLE_ALARM_MIN_MILLI_C (-40000)
#define KFSW_TEMP_EXAMPLE_ALARM_MAX_MILLI_C 125000

/**
 * Zero the read, failure and alarm counters. The cached reading is kept.
 *
 * @return 0, or -EACCES before initialization.
 */
int kfsw_temp_example_clear(void);

/** Parameter table of the temperature example. */
extern const struct kfsw_param_definition_set kfsw_temp_example_param_definitions;

/** Commands of the temperature example, for the app's command registry. */
extern const struct kfsw_command_definition_set kfsw_temp_example_command_definitions;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
