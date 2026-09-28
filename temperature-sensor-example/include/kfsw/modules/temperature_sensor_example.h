#ifndef KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_H
#define KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kfsw_param_definition_set;

/** Parameter table reserved for the temperature example. */
#define KFSW_TEMP_EXAMPLE_TABLE_ID 51U
/** Parameter table name. */
#define KFSW_TEMP_EXAMPLE_TABLE_NAME "temp_example"

/**
 * Sentinel for an invalid or unavailable temperature.
 */
#define KFSW_TEMP_EXAMPLE_INVALID_MILLI_C INT32_MIN

/** @defgroup kfsw_modules_temp_example Temperature sensor example module
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
	/** Whether milli_c is a successful reading within the maximum age. */
	bool valid;
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

/** Parameter table of the temperature example. */
extern const struct kfsw_param_definition_set kfsw_temp_example_param_definitions;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
