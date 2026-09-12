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
/** Stable logical name paired with KFSW_TEMP_EXAMPLE_TABLE_ID. */
#define KFSW_TEMP_EXAMPLE_TABLE_NAME "temp_example"

/**
 * Reading reported when the sensor has not been read successfully.
 *
 * Far outside anything a die can survive, so a consumer that ignores the valid
 * flag still cannot mistake an absent reading for a cold one. Zero would be a
 * plausible temperature, which is exactly what makes it the wrong choice.
 */
#define KFSW_TEMP_EXAMPLE_INVALID_MILLI_C INT32_MIN

/** @defgroup kfsw_modules_temp_example Temperature sensor example module
 *  @ingroup kfsw_modules
 *  A worked example of a sensor behind a parameter table, for housekeeping.
 *
 *  The symbols are prefixed `kfsw_temp_example_` rather than with the full
 *  directory name, which would leave little of a 100-column line for the rest
 *  of the declaration.
 *
 *  @{
 */

/** What the module knows about the sensor since boot. */
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
 * Initialize the module and, when one is composed, bind and start the sensor.
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
 * Cheap: this returns what the poller last stored and never touches the ADC,
 * so it is safe from a parameter sample callback.
 * Readings older than CONFIG_KFSW_TEMP_EXAMPLE_MAX_AGE_MS are returned as invalid.
 *
 * @param reading Destination.
 * @return 0 on success, -EINVAL for a NULL destination, or -EACCES before
 *         initialization.
 */
int kfsw_temp_example_get(struct kfsw_temp_example_reading *reading);

/** Read-only live PARAM definitions owned by the temperature example. */
extern const struct kfsw_param_definition_set kfsw_temp_example_param_definitions;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
