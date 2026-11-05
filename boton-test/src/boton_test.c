#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/modules/boton_test.h>
#include <kfsw/services/log.h>

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

static K_MUTEX_DEFINE(kfsw_boton_test_lock);

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

	if (result != 0) {
		kfsw_log_warning("Button test LED %d not switched: %d", (int)led, result);
	}
	return result;
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
	uint32_t press_count = boton_test_status.press_count;
	k_mutex_unlock(&kfsw_boton_test_lock);

	if (pressed) {
		kfsw_log_info("Button pressed, %u presses since boot", press_count);
	}
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
		kfsw_log_error("Button GPIO not ready: %d", result);
		return result;
	}
#endif

#if CONFIG_KFSW_BOTON_TEST_LED_GPIO
	result = kfsw_boton_test_led_gpio_prepare();
	if (result != 0) {
		k_mutex_unlock(&kfsw_boton_test_lock);
		kfsw_log_error("LED GPIO not ready: %d", result);
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
	if (result != 0) {
		kfsw_log_error("Button test did not start: %d", result);
	} else {
		kfsw_log_info("Button test started");
	}
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
