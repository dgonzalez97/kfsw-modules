#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/temperature_sensor_example.h>
#include <kfsw/services/parameter.h>

/*
 * Die temperature in milli-degrees, with the valid flag and the failure count.
 */

static int32_t temp_milli_c = KFSW_TEMP_EXAMPLE_INVALID_MILLI_C;
static uint32_t temp_samples;
static uint32_t temp_failures;
static uint32_t temp_last_uptime_ms;
static uint8_t temp_valid;

static void sample_reading(void)
{
	struct kfsw_temp_example_reading reading;

	if (kfsw_temp_example_get(&reading) != 0) {
		return;
	}
	temp_milli_c = reading.milli_c;
	temp_samples = reading.samples;
	temp_failures = reading.failures;
	temp_last_uptime_ms = reading.last_uptime_ms;
	temp_valid = reading.valid ? 1U : 0U;
}

#define TEMP_SAMPLE(field, type)                                                                   \
	static void sample_##field(void *value)                                                    \
	{                                                                                          \
		sample_reading();                                                                  \
		*(type *)value = temp_##field;                                                     \
	}

TEMP_SAMPLE(milli_c, int32_t)
TEMP_SAMPLE(samples, uint32_t)
TEMP_SAMPLE(failures, uint32_t)
TEMP_SAMPLE(last_uptime_ms, uint32_t)
TEMP_SAMPLE(valid, uint8_t)

static const struct kfsw_param_definition temp_example_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_LIVE,
		.name = "temp_mcu_mc",
		.unit = "mC",
		.description = "Die temperature in thousandths of a degree Celsius",
		.value = &temp_milli_c,
		.sample = sample_milli_c,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "temp_samples",
		.description = "Successful sensor reads since boot",
		.value = &temp_samples,
		.sample = sample_samples,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "temp_failures",
		.description = "Sensor reads that returned an error",
		.value = &temp_failures,
		.sample = sample_failures,
	},
	{
		.offset = 0x0cU,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "temp_last_ms",
		.unit = "ms",
		.description = "Uptime of the latest successful read",
		.value = &temp_last_uptime_ms,
		.sample = sample_last_uptime_ms,
	},
	{
		.offset = 0x10U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "temp_valid",
		.description = "Whether temp_mcu_mc is a reading or the reserved value",
		.value = &temp_valid,
		.sample = sample_valid,
	},
};

const struct kfsw_param_definition_set kfsw_temp_example_param_definitions = {
	.table = KFSW_TEMP_EXAMPLE_TABLE_ID,
	.name = KFSW_TEMP_EXAMPLE_TABLE_NAME,
	.definitions = temp_example_param_definitions,
	.count = ARRAY_SIZE(temp_example_param_definitions),
};
