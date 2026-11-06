#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>

#include "button_test_internal.h"

#if CONFIG_KFSW_BUTTON_TEST_GPIO
/* TODO(template): the hardware backend. Everything here is bound from
 * devicetree, and the generic layer builds and tests without this file.
 */
#define KFSW_BUTTON_TEST_BUTTON_NODE DT_CHOSEN(kfsw_button_test_button)

BUILD_ASSERT(DT_NODE_EXISTS(KFSW_BUTTON_TEST_BUTTON_NODE),
	     "kfsw,button-test-button must select a GPIO button node");
BUILD_ASSERT(DT_NODE_HAS_PROP(KFSW_BUTTON_TEST_BUTTON_NODE, gpios),
	     "kfsw,button-test-button must provide a gpios property");

static const struct gpio_dt_spec button_test_button =
	GPIO_DT_SPEC_GET(KFSW_BUTTON_TEST_BUTTON_NODE, gpios);
static struct gpio_callback button_test_gpio_callback;

static void button_test_debounce_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(button_test_debounce_work, button_test_debounce_work_handler);

static void button_test_debounce_work_handler(struct k_work *work)
{
	int pressed;

	ARG_UNUSED(work);

	pressed = gpio_pin_get_dt(&button_test_button);
	if (pressed >= 0) {
		kfsw_button_test_process_level(pressed != 0, kfsw_time_monotonic_ms());
	}
}

static void button_test_gpio_edge(const struct device *port, struct gpio_callback *callback,
				  uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&button_test_debounce_work,
				K_MSEC(CONFIG_KFSW_BUTTON_TEST_DEBOUNCE_MS));
}

int kfsw_button_test_gpio_prepare(bool *initially_pressed)
{
	int pressed;
	int result;

	if (initially_pressed == NULL) {
		return -EINVAL;
	}
	if (!gpio_is_ready_dt(&button_test_button)) {
		return -ENODEV;
	}

	result = gpio_pin_configure_dt(&button_test_button, GPIO_INPUT);
	if (result != 0) {
		return result;
	}
	pressed = gpio_pin_get_dt(&button_test_button);
	if (pressed < 0) {
		return pressed;
	}

	*initially_pressed = pressed != 0;
	return 0;
}

int kfsw_button_test_gpio_start(void)
{
	int result;

	gpio_init_callback(&button_test_gpio_callback, button_test_gpio_edge,
			   BIT(button_test_button.pin));
	result = gpio_add_callback(button_test_button.port, &button_test_gpio_callback);
	if (result != 0) {
		return result;
	}
	result = gpio_pin_interrupt_configure_dt(&button_test_button, GPIO_INT_EDGE_BOTH);
	if (result != 0) {
		(void)gpio_remove_callback(button_test_button.port, &button_test_gpio_callback);
		return result;
	}

	/* Reconcile any transition between the initial sample and interrupt enable. */
	(void)k_work_reschedule(&button_test_debounce_work,
				K_MSEC(CONFIG_KFSW_BUTTON_TEST_DEBOUNCE_MS));
	return 0;
}
#endif

#if CONFIG_KFSW_BUTTON_TEST_LED_GPIO
#define KFSW_BUTTON_TEST_GREEN_LED_NODE DT_CHOSEN(kfsw_button_test_led_green)
#define KFSW_BUTTON_TEST_BLUE_LED_NODE DT_CHOSEN(kfsw_button_test_led_blue)
#define KFSW_BUTTON_TEST_RED_LED_NODE DT_CHOSEN(kfsw_button_test_led_red)

#define KFSW_BUTTON_TEST_ASSERT_LED(node_id, chosen_name)                                          \
	BUILD_ASSERT(DT_NODE_EXISTS(node_id), chosen_name " must select a GPIO LED node");         \
	BUILD_ASSERT(DT_NODE_HAS_PROP(node_id, gpios), chosen_name " must provide gpios")

KFSW_BUTTON_TEST_ASSERT_LED(KFSW_BUTTON_TEST_GREEN_LED_NODE, "kfsw,button-test-led-green");
KFSW_BUTTON_TEST_ASSERT_LED(KFSW_BUTTON_TEST_BLUE_LED_NODE, "kfsw,button-test-led-blue");
KFSW_BUTTON_TEST_ASSERT_LED(KFSW_BUTTON_TEST_RED_LED_NODE, "kfsw,button-test-led-red");

static const struct gpio_dt_spec button_test_leds[KFSW_BUTTON_TEST_LED_COUNT] = {
	[KFSW_BUTTON_TEST_LED_GREEN] = GPIO_DT_SPEC_GET(KFSW_BUTTON_TEST_GREEN_LED_NODE, gpios),
	[KFSW_BUTTON_TEST_LED_BLUE] = GPIO_DT_SPEC_GET(KFSW_BUTTON_TEST_BLUE_LED_NODE, gpios),
	[KFSW_BUTTON_TEST_LED_RED] = GPIO_DT_SPEC_GET(KFSW_BUTTON_TEST_RED_LED_NODE, gpios),
};

int kfsw_button_test_led_gpio_prepare(void)
{
	for (size_t index = 0U; index < ARRAY_SIZE(button_test_leds); index++) {
		int result;

		if (!gpio_is_ready_dt(&button_test_leds[index])) {
			return -ENODEV;
		}
		result = gpio_pin_configure_dt(&button_test_leds[index], GPIO_OUTPUT_INACTIVE);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

int kfsw_button_test_led_gpio_set(enum kfsw_button_test_led led, bool on)
{
	if ((unsigned int)led >= ARRAY_SIZE(button_test_leds)) {
		return -EINVAL;
	}
	return gpio_pin_set_dt(&button_test_leds[led], on ? 1 : 0);
}
#endif
