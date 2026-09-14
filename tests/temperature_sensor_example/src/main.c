#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/modules/temperature_sensor_example.h>
#include <kfsw/services/parameter.h>

#include "temperature_sensor_example_internal.h"

/* Offsets within table 51. */
#define TEMP_MILLI_C_OFFSET 0x00U
#define TEMP_SAMPLES_OFFSET 0x04U
#define TEMP_FAILURES_OFFSET 0x08U
#define TEMP_LAST_MS_OFFSET 0x0cU
#define TEMP_VALID_OFFSET 0x10U

static uint64_t now_ms;

uint64_t __wrap_kfsw_time_monotonic_ms(void)
{
	return now_ms;
}

static void store_reading(int32_t milli_c, uint64_t timestamp)
{
	now_ms = timestamp;
	kfsw_temp_example_store(milli_c, timestamp);
}

static int32_t read_i32(uint8_t offset)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_TABLE_ID, offset), &value));
	zassert_equal(value.type, KFSW_PARAM_I32);
	return value.scalar.i32;
}

static uint32_t read_u32(uint8_t offset)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_TABLE_ID, offset), &value));
	zassert_equal(value.type, KFSW_PARAM_U32);
	return value.scalar.u32;
}

static uint8_t read_u8(uint8_t offset)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_TABLE_ID, offset), &value));
	zassert_equal(value.type, KFSW_PARAM_U8);
	return value.scalar.u8;
}

static void *temp_example_setup(void)
{
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_temp_example_param_definitions,
	};

	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_ok(kfsw_temp_example_init());
	return NULL;
}

ZTEST(temp_example, test_starts_absent_not_cold)
{
	struct kfsw_temp_example_reading reading;

	kfsw_temp_example_store_failure();

	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	zassert_equal(reading.milli_c, KFSW_TEMP_EXAMPLE_INVALID_MILLI_C);
	/* An unread sensor must not look like a reading of 0 C. */
	zassert_not_equal(reading.milli_c, 0);
}

ZTEST(temp_example, test_reading_reaches_the_table)
{
	store_reading(23500, 12345U);

	zassert_equal(read_i32(TEMP_MILLI_C_OFFSET), 23500);
	zassert_equal(read_u32(TEMP_LAST_MS_OFFSET), 12345U);
	zassert_equal(read_u8(TEMP_VALID_OFFSET), 1U);
}

ZTEST(temp_example, test_negative_reading_survives_the_table)
{
	/* Readings below zero keep their sign. */
	store_reading(-14250, 1U);

	zassert_equal(read_i32(TEMP_MILLI_C_OFFSET), -14250);
	zassert_equal(read_u8(TEMP_VALID_OFFSET), 1U);
}

ZTEST(temp_example, test_failure_drops_the_stale_reading)
{
	uint32_t failures_before;

	store_reading(30000, 100U);
	zassert_equal(read_i32(TEMP_MILLI_C_OFFSET), 30000);
	failures_before = read_u32(TEMP_FAILURES_OFFSET);

	kfsw_temp_example_store_failure();

	/* A failed read must not keep the last good value. */
	zassert_equal(read_i32(TEMP_MILLI_C_OFFSET), KFSW_TEMP_EXAMPLE_INVALID_MILLI_C);
	zassert_equal(read_u8(TEMP_VALID_OFFSET), 0U);
	zassert_equal(read_u32(TEMP_FAILURES_OFFSET), failures_before + 1U);
}

ZTEST(temp_example, test_counters_separate_success_from_failure)
{
	uint32_t samples_before = read_u32(TEMP_SAMPLES_OFFSET);
	uint32_t failures_before = read_u32(TEMP_FAILURES_OFFSET);

	store_reading(1000, 1U);
	store_reading(2000, 2U);
	kfsw_temp_example_store_failure();

	zassert_equal(read_u32(TEMP_SAMPLES_OFFSET), samples_before + 2U);
	zassert_equal(read_u32(TEMP_FAILURES_OFFSET), failures_before + 1U);
}

ZTEST(temp_example, test_table_is_registered_read_only)
{
	struct kfsw_param_info info;

	zassert_ok(kfsw_param_get_info("temp_mcu_mc", &info));
	zassert_equal(info.table, KFSW_TEMP_EXAMPLE_TABLE_ID);
	zassert_equal(info.offset, TEMP_MILLI_C_OFFSET);
	zassert_equal(info.type, KFSW_PARAM_I32);
	zassert_true(info.read_only);
	/* Housekeeping sizes reports from the declared width. */
	zassert_equal(info.array_size, 1U);
}

ZTEST(temp_example, test_rejects_a_null_destination)
{
	zassert_equal(kfsw_temp_example_get(NULL), -EINVAL);
}

ZTEST_SUITE(temp_example, NULL, temp_example_setup, NULL, NULL, NULL);

ZTEST(temp_example, test_age_limit_and_recovery_across_uptime_wrap)
{
	struct kfsw_temp_example_reading reading;
	uint64_t timestamp = (uint64_t)UINT32_MAX - 5U;

	store_reading(25000, timestamp);
	now_ms += CONFIG_KFSW_TEMP_EXAMPLE_MAX_AGE_MS;
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_true(reading.valid);
	now_ms++;
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	zassert_equal(reading.milli_c, KFSW_TEMP_EXAMPLE_INVALID_MILLI_C);
	zassert_equal(reading.last_uptime_ms, (uint32_t)timestamp);

	now_ms += (UINT64_C(1) << 32);
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_false(reading.valid);
	store_reading(27000, now_ms);
	zassert_ok(kfsw_temp_example_get(&reading));
	zassert_true(reading.valid);
	zassert_equal(reading.milli_c, 27000);
}
