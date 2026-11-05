#include <errno.h>
#include <stdint.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <kfsw/modules/temperature_sensor_example.h>
#include <kfsw/services/command.h>
#include <kfsw/services/event.h>
#include <kfsw/services/parameter.h>

#include "temperature_sensor_example_internal.h"

/* Offsets within table 51. */
#define TEMP_MILLI_C_OFFSET 0x00U
#define TEMP_SAMPLES_OFFSET 0x04U
#define TEMP_FAILURES_OFFSET 0x08U
#define TEMP_LAST_MS_OFFSET 0x0cU
#define TEMP_VALID_OFFSET 0x10U
#define TEMP_ALARMS_OFFSET 0x14U
#define TEMP_ALARM_MC_OFFSET 0x18U

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

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_PARAM_TABLE_ID, offset),
					&value));
	zassert_equal(value.type, KFSW_PARAM_I32);
	return value.scalar.i32;
}

static uint32_t read_u32(uint8_t offset)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_PARAM_TABLE_ID, offset),
					&value));
	zassert_equal(value.type, KFSW_PARAM_U32);
	return value.scalar.u32;
}

static uint8_t read_u8(uint8_t offset)
{
	struct kfsw_param_value value;

	zassert_ok(kfsw_param_get_by_id(KFSW_PARAM_ID(KFSW_TEMP_EXAMPLE_PARAM_TABLE_ID, offset),
					&value));
	zassert_equal(value.type, KFSW_PARAM_U8);
	return value.scalar.u8;
}

static void *temp_example_setup(void)
{
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_temp_example_param_definitions,
	};

	const struct kfsw_command_definition_set *const command_sets[] = {
		&kfsw_temp_example_command_definitions,
	};

	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));
	zassert_ok(kfsw_command_init(command_sets, ARRAY_SIZE(command_sets)));
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
	zassert_equal(info.table, KFSW_TEMP_EXAMPLE_PARAM_TABLE_ID);
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

static void set_alarm(int32_t milli_c)
{
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_I32,
		.size = sizeof(int32_t),
		.scalar.i32 = milli_c,
	};

	zassert_ok(kfsw_param_set("temp_alarm_mc", &value));
}

static void check_last_event(uint16_t id, int32_t milli_c)
{
	struct kfsw_event_record record;

	zassert_ok(kfsw_event_get(0U, &record));
	zassert_equal(record.source, KFSW_EVENT_SOURCE_APP);
	zassert_equal(record.id, id);
	zassert_equal(record.payload_size, sizeof(int32_t));
	zassert_equal((int32_t)sys_get_be32(record.payload), milli_c);
}

ZTEST(temp_example, test_alarm_raises_and_clears_one_event_each)
{
	struct kfsw_event_stats before;
	struct kfsw_event_stats after;
	uint32_t alarms_before = read_u32(TEMP_ALARMS_OFFSET);

	set_alarm(30000);
	store_reading(29000, 10U);
	kfsw_event_get_stats(&before);

	store_reading(31000, 11U);
	check_last_event(KFSW_EVENT_TEMP_EXAMPLE_ALARM, 31000);
	store_reading(32000, 12U);
	kfsw_event_get_stats(&after);
	zassert_equal(after.recorded, before.recorded + 1U, "a held alarm repeated its event");
	zassert_equal(read_u32(TEMP_ALARMS_OFFSET), alarms_before + 1U);

	store_reading(20000, 13U);
	check_last_event(KFSW_EVENT_TEMP_EXAMPLE_ALARM_CLEARED, 20000);

	set_alarm(CONFIG_KFSW_TEMP_EXAMPLE_ALARM_MILLI_C);
}

ZTEST(temp_example, test_alarm_setting_is_a_persistent_configuration)
{
	struct kfsw_param_info info;
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_I32,
		.size = sizeof(int32_t),
		.scalar.i32 = KFSW_TEMP_EXAMPLE_ALARM_MAX_MILLI_C + 1,
	};

	zassert_ok(kfsw_param_get_info("temp_alarm_mc", &info));
	zassert_equal(info.offset, TEMP_ALARM_MC_OFFSET);
	zassert_false(info.read_only);
	zassert_true((info.flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	zassert_true((info.flags & KFSW_PARAM_FLAG_CONFIGURATION) != 0U);
	zassert_equal(read_i32(TEMP_ALARM_MC_OFFSET), CONFIG_KFSW_TEMP_EXAMPLE_ALARM_MILLI_C);

	/* Out of range is refused and leaves the stored value alone. */
	zassert_not_equal(kfsw_param_set("temp_alarm_mc", &value), 0);
	zassert_equal(read_i32(TEMP_ALARM_MC_OFFSET), CONFIG_KFSW_TEMP_EXAMPLE_ALARM_MILLI_C);
	zassert_equal(kfsw_temp_example_set_alarm(KFSW_TEMP_EXAMPLE_ALARM_MIN_MILLI_C - 1),
		      -ERANGE);
}

ZTEST(temp_example, test_clear_command_zeroes_the_counters)
{
	struct kfsw_command_result result = {0};
	struct kfsw_command_info info;

	store_reading(1000, 20U);
	kfsw_temp_example_store_failure();
	zassert_ok(kfsw_command_find("temp_clear", &info));
	zassert_equal(info.id, KFSW_COMMAND_ID_TEMP_EXAMPLE_CLEAR);

	zassert_ok(kfsw_command_invoke("temp_clear", NULL, 0U, &result));
	zassert_equal(result.status, KFSW_COMMAND_OK);
	zassert_equal(read_u32(TEMP_SAMPLES_OFFSET), 0U);
	zassert_equal(read_u32(TEMP_FAILURES_OFFSET), 0U);
	zassert_equal(read_u32(TEMP_ALARMS_OFFSET), 0U);

	/* A wrong argument count is refused before the handler runs. */
	struct kfsw_command_arg extra = {.type = KFSW_COMMAND_TYPE_U32, .value.u32 = 1U};

	zassert_not_equal(kfsw_command_invoke("temp_clear", &extra, 1U, &result), 0);
}
