#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#if CONFIG_KFSW_BOTON_TEST_SHELL
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/util.h>

#include <kfsw/modules/boton_test.h>
#include <kfsw/services/parameter.h>

#include "boton_test_internal.h"

struct boton_test_state {
	uint32_t press_count;
	uint32_t last_press_s;
	uint8_t led_green;
	uint8_t led_blue;
	uint8_t led_red;
};

static struct boton_test_state boton_test_status;
static bool boton_test_stable_pressed;
static bool boton_test_initialized;

K_MUTEX_DEFINE(kfsw_boton_test_lock);

static uint32_t monotonic_seconds(uint64_t monotonic_ms)
{
	const uint64_t seconds = monotonic_ms / 1000U;

	return (seconds > UINT32_MAX) ? UINT32_MAX : (uint32_t)seconds;
}

void kfsw_boton_test_state_reset(bool initially_pressed)
{
	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
#if CONFIG_KFSW_BOTON_TEST_LED_GPIO
	(void)kfsw_boton_test_led_gpio_set(KFSW_BOTON_TEST_LED_GREEN, false);
	(void)kfsw_boton_test_led_gpio_set(KFSW_BOTON_TEST_LED_BLUE, false);
	(void)kfsw_boton_test_led_gpio_set(KFSW_BOTON_TEST_LED_RED, false);
#endif
	boton_test_status = (struct boton_test_state){0};
	boton_test_stable_pressed = initially_pressed;
	boton_test_initialized = true;
	k_mutex_unlock(&kfsw_boton_test_lock);
}

static uint8_t *led_state(enum kfsw_boton_test_led led)
{
	switch (led) {
	case KFSW_BOTON_TEST_LED_GREEN:
		return &boton_test_status.led_green;
	case KFSW_BOTON_TEST_LED_BLUE:
		return &boton_test_status.led_blue;
	case KFSW_BOTON_TEST_LED_RED:
		return &boton_test_status.led_red;
	default:
		return NULL;
	}
}

static int set_led_locked(enum kfsw_boton_test_led led, bool on)
{
	uint8_t *state = led_state(led);
	int result = 0;

	if (state == NULL) {
		return -EINVAL;
	}

#if CONFIG_KFSW_BOTON_TEST_LED_GPIO
	result = kfsw_boton_test_led_gpio_set(led, on);
#endif
	if (result == 0) {
		*state = on ? 1U : 0U;
	}
	return result;
}

int kfsw_boton_test_set_led(enum kfsw_boton_test_led led, bool on)
{
	int result;

	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	if (!boton_test_initialized) {
		result = -EACCES;
	} else {
		result = set_led_locked(led, on);
	}
	k_mutex_unlock(&kfsw_boton_test_lock);
	return result;
}

static int validate_led(enum kfsw_boton_test_led led,
			const union kfsw_param_scalar *value)
{
	int result = 0;

	if ((value == NULL) || (value->u8 > 1U)) {
		return -ERANGE;
	}

	/* PARAM validates defaults before module initialization. Runtime writes
	 * apply the GPIO first so a hardware failure rejects the PARAM write.
	 */
	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	if (boton_test_initialized) {
		result = set_led_locked(led, value->u8 != 0U);
	}
	k_mutex_unlock(&kfsw_boton_test_lock);
	return result;
}

static int validate_green_led(const union kfsw_param_scalar *value)
{
	return validate_led(KFSW_BOTON_TEST_LED_GREEN, value);
}

static int validate_blue_led(const union kfsw_param_scalar *value)
{
	return validate_led(KFSW_BOTON_TEST_LED_BLUE, value);
}

static int validate_red_led(const union kfsw_param_scalar *value)
{
	return validate_led(KFSW_BOTON_TEST_LED_RED, value);
}

void kfsw_boton_test_process_level(bool pressed, uint64_t monotonic_ms)
{
	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	if (!boton_test_initialized || (pressed == boton_test_stable_pressed)) {
		k_mutex_unlock(&kfsw_boton_test_lock);
		return;
	}

	boton_test_stable_pressed = pressed;
	if (pressed) {
		if (boton_test_status.press_count < UINT32_MAX) {
			boton_test_status.press_count++;
		}
		boton_test_status.last_press_s = monotonic_seconds(monotonic_ms);
	}
	k_mutex_unlock(&kfsw_boton_test_lock);
}

#if CONFIG_ZTEST
void kfsw_boton_test_test_disable(void)
{
	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	boton_test_initialized = false;
	k_mutex_unlock(&kfsw_boton_test_lock);
}

void kfsw_boton_test_test_set_press_count(uint32_t press_count)
{
	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	boton_test_status.press_count = press_count;
	k_mutex_unlock(&kfsw_boton_test_lock);
}
#endif

int kfsw_boton_test_init(void)
{
	bool initially_pressed = false;
	int result = 0;

	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	if (boton_test_initialized) {
		k_mutex_unlock(&kfsw_boton_test_lock);
		return 0;
	}

#if CONFIG_KFSW_BOTON_TEST_GPIO
	result = kfsw_boton_test_gpio_prepare(&initially_pressed);
	if (result != 0) {
		k_mutex_unlock(&kfsw_boton_test_lock);
		return result;
	}
#endif

#if CONFIG_KFSW_BOTON_TEST_LED_GPIO
	result = kfsw_boton_test_led_gpio_prepare();
	if (result != 0) {
		k_mutex_unlock(&kfsw_boton_test_lock);
		return result;
	}
#endif

	boton_test_status = (struct boton_test_state){0};
	boton_test_stable_pressed = initially_pressed;
	boton_test_initialized = true;

#if CONFIG_KFSW_BOTON_TEST_GPIO
	result = kfsw_boton_test_gpio_start();
	if (result != 0) {
		boton_test_initialized = false;
	}
#endif

	k_mutex_unlock(&kfsw_boton_test_lock);
	return result;
}

int kfsw_boton_test_get_status(struct kfsw_boton_test_status *status)
{
	int result = 0;

	if (status == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&kfsw_boton_test_lock, K_FOREVER);
	if (!boton_test_initialized) {
		result = -EACCES;
	} else {
		status->press_count = boton_test_status.press_count;
		status->last_press_s = boton_test_status.last_press_s;
		status->led_green = boton_test_status.led_green != 0U;
		status->led_blue = boton_test_status.led_blue != 0U;
		status->led_red = boton_test_status.led_red != 0U;
	}
	k_mutex_unlock(&kfsw_boton_test_lock);
	return result;
}

static const struct kfsw_param_definition boton_test_param_definitions[] = {
	{
		.id = 6U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_DEBUG,
		.name = "boton_test_press_count",
		.unit = "count",
		.description = "Valid debounced button presses since boot",
		.value = &boton_test_status.press_count,
		.default_value = {.u32 = 0U},
	},
	{
		.id = 7U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_DEBUG,
		.name = "boton_test_last_press_s",
		.unit = "s",
		.description = "Monotonic whole seconds of the latest valid press",
		.value = &boton_test_status.last_press_s,
		.default_value = {.u32 = 0U},
	},
	{
		.id = 8U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "hw_test_led_green",
		.description = "Logical green developer LED state",
		.value = &boton_test_status.led_green,
		.default_value = {.u8 = 0U},
		.validate = validate_green_led,
	},
	{
		.id = 9U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "hw_test_led_blue",
		.description = "Logical blue developer LED state",
		.value = &boton_test_status.led_blue,
		.default_value = {.u8 = 0U},
		.validate = validate_blue_led,
	},
	{
		.id = 10U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_DEBUG,
		.name = "hw_test_led_red",
		.description = "Logical red developer LED state",
		.value = &boton_test_status.led_red,
		.default_value = {.u8 = 0U},
		.validate = validate_red_led,
	},
};

const struct kfsw_param_definition_set kfsw_boton_test_param_definitions = {
	.definitions = boton_test_param_definitions,
	.count = ARRAY_SIZE(boton_test_param_definitions),
};

#if CONFIG_KFSW_BOTON_TEST_SHELL
static int cmd_boton_test_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_boton_test_status status;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_boton_test_get_status(&status);
	if (result != 0) {
		shell_error(sh, "boton_test status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "press_count: %u", status.press_count);
	shell_print(sh, "last_press_s: %u", status.last_press_s);
	shell_print(sh, "led_green: %s", status.led_green ? "on" : "off");
	shell_print(sh, "led_blue: %s", status.led_blue ? "on" : "off");
	shell_print(sh, "led_red: %s", status.led_red ? "on" : "off");
#if CONFIG_KFSW_BOTON_TEST_GPIO
	shell_print(sh, "debounce_ms: %d", CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS);
#endif
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(boton_test_commands,
	SHELL_CMD_ARG(status, NULL, "Show debounced button status.", cmd_boton_test_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(boton_test, &boton_test_commands, "K-FSW button example diagnostics.", NULL);

static int cmd_hw_test_led(const struct shell *sh, size_t argc, char **argv)
{
	enum kfsw_boton_test_led led;
	bool on;
	int result;

	ARG_UNUSED(argc);

	if (strcmp(argv[1], "green") == 0) {
		led = KFSW_BOTON_TEST_LED_GREEN;
	} else if (strcmp(argv[1], "blue") == 0) {
		led = KFSW_BOTON_TEST_LED_BLUE;
	} else if (strcmp(argv[1], "red") == 0) {
		led = KFSW_BOTON_TEST_LED_RED;
	} else {
		shell_error(sh, "unknown LED colour: %s", argv[1]);
		return -EINVAL;
	}

	if (strcmp(argv[2], "on") == 0) {
		on = true;
	} else if (strcmp(argv[2], "off") == 0) {
		on = false;
	} else {
		shell_error(sh, "LED state must be on or off");
		return -EINVAL;
	}

	result = kfsw_boton_test_set_led(led, on);
	if (result != 0) {
		shell_error(sh, "%s LED control failed (%d)", argv[1], result);
		return result;
	}

	shell_print(sh, "%s: %s", argv[1], argv[2]);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(hw_test_commands,
	SHELL_CMD_ARG(led, NULL, "Control LED: led <green|blue|red> <on|off>.",
		      cmd_hw_test_led, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(test, &hw_test_commands, "K-FSW developer hardware-test commands.", NULL);
#endif
