#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#if CONFIG_KFSW_BOTON_TEST_GPIO
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#endif

#include <kfsw/modules/boton_test.h>
#include <kfsw/services/parameter.h>

#include "boton_test_internal.h"

#define BOTON_TEST_PRESS_COUNT_ID 6U
#define BOTON_TEST_LAST_PRESS_S_ID 7U
#define HW_TEST_LED_GREEN_ID 8U
#define HW_TEST_LED_BLUE_ID 9U
#define HW_TEST_LED_RED_ID 10U

#if CONFIG_KFSW_BOTON_TEST_GPIO
#define BOTON_TEST_BUTTON_NODE DT_CHOSEN(kfsw_boton_test_button)
#define BOTON_TEST_BOUNCE_STEP_MS 5U
#define BOTON_TEST_SETTLE_MARGIN_MS 15U

static const struct gpio_dt_spec boton_test_button =
	GPIO_DT_SPEC_GET(BOTON_TEST_BUTTON_NODE, gpios);
static const struct gpio_dt_spec boton_test_green_led =
	GPIO_DT_SPEC_GET(DT_CHOSEN(kfsw_boton_test_led_green), gpios);
static const struct gpio_dt_spec boton_test_blue_led =
	GPIO_DT_SPEC_GET(DT_CHOSEN(kfsw_boton_test_led_blue), gpios);
static const struct gpio_dt_spec boton_test_red_led =
	GPIO_DT_SPEC_GET(DT_CHOSEN(kfsw_boton_test_led_red), gpios);

static void set_raw_button_level(int value)
{
	zassert_ok(gpio_emul_input_set(boton_test_button.port, boton_test_button.pin, value));
}

static void wait_for_debounce(void)
{
	k_sleep(K_MSEC(CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS + BOTON_TEST_SETTLE_MARGIN_MS));
}
#endif

struct parameter_visit_summary {
	size_t count;
	bool saw_press_count;
	bool saw_last_press_s;
	bool saw_led_green;
	bool saw_led_blue;
	bool saw_led_red;
	bool button_values_read_only;
	bool led_values_writable;
	bool any_persistent;
};

static const struct kfsw_param_definition *find_definition(const char *name)
{
	for (size_t index = 0U; index < kfsw_boton_test_param_definitions.count; index++) {
		const struct kfsw_param_definition *definition =
			&kfsw_boton_test_param_definitions.definitions[index];

		if (strcmp(definition->name, name) == 0) {
			return definition;
		}
	}

	return NULL;
}

static bool summarize_parameter(const struct kfsw_param_info *info, void *context)
{
	struct parameter_visit_summary *summary = context;

	summary->count++;
	summary->saw_press_count |= strcmp(info->name, "boton_test_press_count") == 0;
	summary->saw_last_press_s |= strcmp(info->name, "boton_test_last_press_s") == 0;
	summary->saw_led_green |= strcmp(info->name, "hw_test_led_green") == 0;
	summary->saw_led_blue |= strcmp(info->name, "hw_test_led_blue") == 0;
	summary->saw_led_red |= strcmp(info->name, "hw_test_led_red") == 0;
	if ((strcmp(info->name, "boton_test_press_count") == 0) ||
	    (strcmp(info->name, "boton_test_last_press_s") == 0)) {
		summary->button_values_read_only &= info->read_only;
	}
	if (strncmp(info->name, "hw_test_led_", strlen("hw_test_led_")) == 0) {
		summary->led_values_writable &= !info->read_only;
	}
	summary->any_persistent |= (info->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U;
	return true;
}

static void *boton_test_setup(void)
{
	const struct kfsw_param_definition_set *const parameter_sets[] = {
		&kfsw_boton_test_param_definitions,
	};

	zassert_ok(kfsw_param_init(parameter_sets, ARRAY_SIZE(parameter_sets)));

#if CONFIG_KFSW_BOTON_TEST_GPIO
	zassert_true(gpio_is_ready_dt(&boton_test_button));
	zassert_ok(gpio_pin_configure_dt(&boton_test_button, GPIO_INPUT));
	set_raw_button_level(1);
#endif

	zassert_ok(kfsw_boton_test_init());
	return NULL;
}

static void boton_test_before(void *fixture)
{
	ARG_UNUSED(fixture);

#if CONFIG_KFSW_BOTON_TEST_GPIO
	set_raw_button_level(1);
	wait_for_debounce();
#endif

	kfsw_boton_test_state_reset(false);
}

ZTEST(boton_test, test_initial_status_and_null_destination)
{
	struct kfsw_boton_test_status status = {
		.press_count = UINT32_MAX,
		.last_press_s = UINT32_MAX,
	};

	zassert_equal(kfsw_boton_test_get_status(NULL), -EINVAL);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 0U);
	zassert_equal(status.last_press_s, 0U);
	zassert_false(status.led_green);
	zassert_false(status.led_blue);
	zassert_false(status.led_red);
}

ZTEST(boton_test, test_status_is_unavailable_before_initialization)
{
	struct kfsw_boton_test_status status = {
		.press_count = UINT32_MAX,
		.last_press_s = UINT32_MAX,
		.led_green = true,
		.led_blue = true,
		.led_red = true,
	};

	kfsw_boton_test_test_disable();
	zassert_equal(kfsw_boton_test_get_status(&status), -EACCES);
	zassert_equal(status.press_count, UINT32_MAX);
	zassert_equal(status.last_press_s, UINT32_MAX);
	zassert_true(status.led_green);
	zassert_true(status.led_blue);
	zassert_true(status.led_red);
	kfsw_boton_test_state_reset(false);
}

ZTEST(boton_test, test_first_press_uses_floored_monotonic_seconds)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_process_level(true, 999U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, 0U);

	kfsw_boton_test_state_reset(false);
	kfsw_boton_test_process_level(true, 1234U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, 1U);
}

ZTEST(boton_test, test_hold_does_not_recount_and_release_rearms)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_process_level(true, 1000U);
	kfsw_boton_test_process_level(true, 2000U);
	kfsw_boton_test_process_level(true, 3000U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, 1U);

	kfsw_boton_test_process_level(false, 3500U);
	kfsw_boton_test_process_level(true, 4999U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 2U);
	zassert_equal(status.last_press_s, 4U);
}

ZTEST(boton_test, test_repeated_stable_samples_do_not_create_bounce_counts)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_process_level(true, 1000U);
	kfsw_boton_test_process_level(true, 1001U);
	kfsw_boton_test_process_level(true, 1002U);
	kfsw_boton_test_process_level(false, 1100U);
	kfsw_boton_test_process_level(false, 1101U);
	kfsw_boton_test_process_level(true, 2200U);
	kfsw_boton_test_process_level(true, 2201U);

	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 2U);
	zassert_equal(status.last_press_s, 2U);
}

ZTEST(boton_test, test_reset_clears_state_and_initially_held_button_is_not_a_press)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_process_level(true, 1000U);
	kfsw_boton_test_state_reset(true);
	kfsw_boton_test_process_level(true, 2000U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 0U);
	zassert_equal(status.last_press_s, 0U);

	kfsw_boton_test_process_level(false, 2100U);
	kfsw_boton_test_process_level(true, 3000U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, 3U);
}

ZTEST(boton_test, test_saturated_press_count_still_updates_timestamp)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_test_set_press_count(UINT32_MAX);
	kfsw_boton_test_process_level(true, 1000U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, UINT32_MAX);
	zassert_equal(status.last_press_s, 1U);

	kfsw_boton_test_process_level(false, 2000U);
	kfsw_boton_test_process_level(true, 5000U);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, UINT32_MAX);
	zassert_equal(status.last_press_s, 5U);
}

ZTEST(boton_test, test_timestamp_saturates_instead_of_wrapping)
{
	struct kfsw_boton_test_status status;
	const uint64_t beyond_u32_seconds = ((uint64_t)UINT32_MAX + 1U) * 1000U;

	kfsw_boton_test_process_level(true, beyond_u32_seconds);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, UINT32_MAX);
}

ZTEST(boton_test, test_parameter_definition_set_has_stable_nonpersistent_ids)
{
	const struct kfsw_param_definition *press_count = find_definition("boton_test_press_count");
	const struct kfsw_param_definition *last_press_s =
		find_definition("boton_test_last_press_s");
	const struct kfsw_param_definition *led_green = find_definition("hw_test_led_green");
	const struct kfsw_param_definition *led_blue = find_definition("hw_test_led_blue");
	const struct kfsw_param_definition *led_red = find_definition("hw_test_led_red");

	zassert_equal(KFSW_HW_TEST_TABLE_ID, 67U);
	zassert_equal(strcmp(KFSW_HW_TEST_TABLE_NAME, "hw_test"), 0);
	zassert_equal(kfsw_boton_test_param_definitions.count, 5U);
	zassert_not_null(press_count);
	zassert_not_null(last_press_s);
	zassert_not_null(led_green);
	zassert_not_null(led_blue);
	zassert_not_null(led_red);
	zassert_equal(press_count->id, BOTON_TEST_PRESS_COUNT_ID);
	zassert_equal(last_press_s->id, BOTON_TEST_LAST_PRESS_S_ID);
	zassert_equal(led_green->id, HW_TEST_LED_GREEN_ID);
	zassert_equal(led_blue->id, HW_TEST_LED_BLUE_ID);
	zassert_equal(led_red->id, HW_TEST_LED_RED_ID);
	zassert_equal(press_count->type, KFSW_PARAM_U32);
	zassert_equal(last_press_s->type, KFSW_PARAM_U32);
	zassert_equal(led_green->type, KFSW_PARAM_U8);
	zassert_equal(led_blue->type, KFSW_PARAM_U8);
	zassert_equal(led_red->type, KFSW_PARAM_U8);
	zassert_true((press_count->flags & KFSW_PARAM_FLAG_READ_ONLY) != 0U);
	zassert_true((last_press_s->flags & KFSW_PARAM_FLAG_READ_ONLY) != 0U);
	zassert_false((led_green->flags & KFSW_PARAM_FLAG_READ_ONLY) != 0U);
	zassert_false((led_blue->flags & KFSW_PARAM_FLAG_READ_ONLY) != 0U);
	zassert_false((led_red->flags & KFSW_PARAM_FLAG_READ_ONLY) != 0U);
	zassert_false((press_count->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	zassert_false((last_press_s->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	zassert_false((led_green->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	zassert_false((led_blue->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
	zassert_false((led_red->flags & KFSW_PARAM_FLAG_PERSISTENT) != 0U);
}

ZTEST(boton_test, test_parameter_visit_contains_hw_test_values)
{
	struct parameter_visit_summary summary = {
		.button_values_read_only = true,
		.led_values_writable = true,
	};

	zassert_ok(kfsw_param_visit(summarize_parameter, &summary));
	zassert_equal(summary.count, 5U);
	zassert_true(summary.saw_press_count);
	zassert_true(summary.saw_last_press_s);
	zassert_true(summary.saw_led_green);
	zassert_true(summary.saw_led_blue);
	zassert_true(summary.saw_led_red);
	zassert_true(summary.button_values_read_only);
	zassert_true(summary.led_values_writable);
	zassert_false(summary.any_persistent);
}

ZTEST(boton_test, test_owner_setter_controls_each_led_without_changing_button)
{
	struct kfsw_boton_test_status status;

	kfsw_boton_test_process_level(true, 5000U);

	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_GREEN, true));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_true(status.led_green);
	zassert_false(status.led_blue);
	zassert_false(status.led_red);
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_GREEN, false));

	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_BLUE, true));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_false(status.led_green);
	zassert_true(status.led_blue);
	zassert_false(status.led_red);
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_BLUE, false));

	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_RED, true));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_false(status.led_green);
	zassert_false(status.led_blue);
	zassert_true(status.led_red);
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_RED, false));

	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);
	zassert_equal(status.last_press_s, 5U);
	zassert_equal(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_COUNT, true), -EINVAL);
}

ZTEST(boton_test, test_param_led_writes_share_owner_state_and_reject_invalid_boolean)
{
	struct kfsw_boton_test_status status;
	struct kfsw_param_value value = {
		.type = KFSW_PARAM_U8,
		.size = sizeof(uint8_t),
		.scalar.u8 = 1U,
	};

	zassert_ok(kfsw_param_set("hw_test_led_green", &value));
	zassert_ok(kfsw_param_set("hw_test_led_blue", &value));
	zassert_ok(kfsw_param_set("hw_test_led_red", &value));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_true(status.led_green);
	zassert_true(status.led_blue);
	zassert_true(status.led_red);

	value.scalar.u8 = 2U;
	zassert_equal(kfsw_param_set("hw_test_led_blue", &value), -ERANGE);
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_true(status.led_green);
	zassert_true(status.led_blue);
	zassert_true(status.led_red);

	value.scalar.u8 = 0U;
	zassert_ok(kfsw_param_set("hw_test_led_green", &value));
	zassert_ok(kfsw_param_set("hw_test_led_blue", &value));
	zassert_ok(kfsw_param_set("hw_test_led_red", &value));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_false(status.led_green);
	zassert_false(status.led_blue);
	zassert_false(status.led_red);
}

ZTEST(boton_test, test_parameter_get_is_live_and_writes_leave_owner_state_unchanged)
{
	struct kfsw_boton_test_status before;
	struct kfsw_boton_test_status after;
	struct kfsw_param_value press_count;
	struct kfsw_param_value last_press_s;

	kfsw_boton_test_process_level(true, 42001U);
	zassert_ok(kfsw_boton_test_get_status(&before));
	zassert_ok(kfsw_param_get("boton_test_press_count", &press_count));
	zassert_ok(kfsw_param_get("boton_test_last_press_s", &last_press_s));
	zassert_equal(press_count.type, KFSW_PARAM_U32);
	zassert_equal(press_count.size, sizeof(uint32_t));
	zassert_equal(press_count.scalar.u32, before.press_count);
	zassert_equal(last_press_s.type, KFSW_PARAM_U32);
	zassert_equal(last_press_s.size, sizeof(uint32_t));
	zassert_equal(last_press_s.scalar.u32, before.last_press_s);

	press_count.scalar.u32 = 100U;
	last_press_s.scalar.u32 = 100U;
	zassert_equal(kfsw_param_set("boton_test_press_count", &press_count), -EACCES);
	zassert_equal(kfsw_param_set("boton_test_last_press_s", &last_press_s), -EACCES);
	zassert_ok(kfsw_boton_test_get_status(&after));
	zassert_equal(after.press_count, before.press_count);
	zassert_equal(after.last_press_s, before.last_press_s);
}

#if CONFIG_KFSW_BOTON_TEST_GPIO
ZTEST(boton_test, test_led_gpio_outputs_start_off_and_honor_devicetree_polarity)
{
	zassert_equal(gpio_emul_output_get_dt(&boton_test_green_led), 0);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_blue_led), 1);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_red_led), 0);

	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_GREEN, true));
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_BLUE, true));
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_RED, true));
	zassert_equal(gpio_emul_output_get_dt(&boton_test_green_led), 1);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_blue_led), 0);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_red_led), 1);

	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_GREEN, false));
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_BLUE, false));
	zassert_ok(kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_RED, false));
	zassert_equal(gpio_emul_output_get_dt(&boton_test_green_led), 0);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_blue_led), 1);
	zassert_equal(gpio_emul_output_get_dt(&boton_test_red_led), 0);
}

ZTEST(boton_test, test_active_low_gpio_isr_debounce_hold_and_release_rearm)
{
	struct kfsw_boton_test_status status;

	/* Raw low is a logical press. Every bounce edge restarts the 30 ms delay. */
	set_raw_button_level(0);
	k_sleep(K_MSEC(BOTON_TEST_BOUNCE_STEP_MS));
	set_raw_button_level(1);
	k_sleep(K_MSEC(BOTON_TEST_BOUNCE_STEP_MS));
	set_raw_button_level(0);
	k_sleep(K_MSEC(BOTON_TEST_BOUNCE_STEP_MS));
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 0U);

	wait_for_debounce();
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);

	/* A held raw-low input emits no new edge and must remain one press. */
	wait_for_debounce();
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);

	/* Raw high is the active-low release; debounce it before pressing again. */
	set_raw_button_level(1);
	wait_for_debounce();
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 1U);

	set_raw_button_level(0);
	wait_for_debounce();
	zassert_ok(kfsw_boton_test_get_status(&status));
	zassert_equal(status.press_count, 2U);
}
#endif

ZTEST_SUITE(boton_test, NULL, boton_test_setup, boton_test_before, NULL, NULL);
