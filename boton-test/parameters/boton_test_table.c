#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/boton_test.h>
#include <kfsw/services/parameter.h>

/*
 * The table keeps its own copy of each value and refreshes it from the module's
 * snapshot, so the module's lock never covers what the parameter service reads.
 * The names predate the prefix rule and are kept because the ground uses them.
 *
 * TODO(template): one definition per value the ground needs; the LEDs show a
 * setting whose write can fail, so it uses apply.
 */

static uint32_t press_count;
static uint32_t last_press_s;
static uint8_t led_green;
static uint8_t led_blue;
static uint8_t led_red;

static struct kfsw_boton_test_status sample_status(void)
{
	struct kfsw_boton_test_status status = {
		.press_count = press_count,
		.last_press_s = last_press_s,
		.led_green = led_green != 0U,
		.led_blue = led_blue != 0U,
		.led_red = led_red != 0U,
	};

	(void)kfsw_boton_test_get_status(&status);
	return status;
}

static void sample_press_count(void *value)
{
	*(uint32_t *)value = sample_status().press_count;
}

static void sample_last_press_s(void *value)
{
	*(uint32_t *)value = sample_status().last_press_s;
}

static void sample_led_green(void *value)
{
	*(uint8_t *)value = sample_status().led_green ? 1U : 0U;
}

static void sample_led_blue(void *value)
{
	*(uint8_t *)value = sample_status().led_blue ? 1U : 0U;
}

static void sample_led_red(void *value)
{
	*(uint8_t *)value = sample_status().led_red ? 1U : 0U;
}

static int validate_led(const union kfsw_param_scalar *value)
{
	return ((value == NULL) || (value->u8 > 1U)) ? -ERANGE : 0;
}

static int apply_green_led(const union kfsw_param_scalar *value)
{
	return kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_GREEN, value->u8 != 0U);
}

static int apply_blue_led(const union kfsw_param_scalar *value)
{
	return kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_BLUE, value->u8 != 0U);
}

static int apply_red_led(const union kfsw_param_scalar *value)
{
	return kfsw_boton_test_set_led(KFSW_BOTON_TEST_LED_RED, value->u8 != 0U);
}

static const struct kfsw_param_definition boton_test_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_DEBUG,
		.name = "press_count",
		.unit = "count",
		.description = "Valid debounced button presses since boot",
		.value = &press_count,
		.sample = sample_press_count,
	},
	{
		.offset = 0x04U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_DEBUG,
		.name = "last_press_s",
		.unit = "s",
		.description = "Monotonic whole seconds of the latest valid press",
		.value = &last_press_s,
		.sample = sample_last_press_s,
	},
	{
		.offset = 0x08U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "led_green",
		.description = "Logical green developer LED state",
		.value = &led_green,
		.validate = validate_led,
		.apply = apply_green_led,
		.sample = sample_led_green,
	},
	{
		.offset = 0x09U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "led_blue",
		.description = "Logical blue developer LED state",
		.value = &led_blue,
		.validate = validate_led,
		.apply = apply_blue_led,
		.sample = sample_led_blue,
	},
	{
		.offset = 0x0aU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "led_red",
		.description = "Logical red developer LED state",
		.value = &led_red,
		.validate = validate_led,
		.apply = apply_red_led,
		.sample = sample_led_red,
	},
};

KFSW_PARAM_TABLE_FITS_THE_LINK(boton_test_param_definitions);

const struct kfsw_param_definition_set kfsw_boton_test_param_definitions = {
	.table = KFSW_BOTON_TEST_PARAM_TABLE_ID,
	.name = KFSW_BOTON_TEST_PARAM_TABLE_NAME,
	.description = "Button presses and LED controls",
	.definitions = boton_test_param_definitions,
	.count = ARRAY_SIZE(boton_test_param_definitions),
};
