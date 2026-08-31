#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#if CONFIG_KFSW_BOTON_TEST_SHELL
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/util.h>

#include <kfsw/modules/boton_test.h>
#include <kfsw/services/parameter.h>

#include "boton_test_internal.h"

static struct kfsw_boton_test_status boton_test_status;
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
	boton_test_status = (struct kfsw_boton_test_status){0};
	boton_test_stable_pressed = initially_pressed;
	boton_test_initialized = true;
	k_mutex_unlock(&kfsw_boton_test_lock);
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

	boton_test_status = (struct kfsw_boton_test_status){0};
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
		*status = boton_test_status;
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
#if CONFIG_KFSW_BOTON_TEST_GPIO
	shell_print(sh, "debounce_ms: %d", CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS);
#endif
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(boton_test_commands,
	SHELL_CMD_ARG(status, NULL, "Show debounced button status.", cmd_boton_test_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(boton_test, &boton_test_commands, "K-FSW button example diagnostics.", NULL);
#endif
