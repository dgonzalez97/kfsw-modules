#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <kfsw/platform/time.h>

#include "boton_test_internal.h"

#define KFSW_BOTON_TEST_BUTTON_NODE DT_CHOSEN(kfsw_boton_test_button)

BUILD_ASSERT(DT_NODE_EXISTS(KFSW_BOTON_TEST_BUTTON_NODE),
	     "kfsw,boton-test-button must select a GPIO button node");
BUILD_ASSERT(DT_NODE_HAS_PROP(KFSW_BOTON_TEST_BUTTON_NODE, gpios),
	     "kfsw,boton-test-button must provide a gpios property");

static const struct gpio_dt_spec boton_test_button =
	GPIO_DT_SPEC_GET(KFSW_BOTON_TEST_BUTTON_NODE, gpios);
static struct gpio_callback boton_test_gpio_callback;

static void boton_test_debounce_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(boton_test_debounce_work, boton_test_debounce_work_handler);

static void boton_test_debounce_work_handler(struct k_work *work)
{
	int pressed;

	ARG_UNUSED(work);

	pressed = gpio_pin_get_dt(&boton_test_button);
	if (pressed >= 0) {
		kfsw_boton_test_process_level(pressed != 0, kfsw_time_monotonic_ms());
	}
}

static void boton_test_gpio_edge(const struct device *port, struct gpio_callback *callback,
				 uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&boton_test_debounce_work,
				K_MSEC(CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS));
}

int kfsw_boton_test_gpio_prepare(bool *initially_pressed)
{
	int pressed;
	int result;

	if (initially_pressed == NULL) {
		return -EINVAL;
	}
	if (!gpio_is_ready_dt(&boton_test_button)) {
		return -ENODEV;
	}

	result = gpio_pin_configure_dt(&boton_test_button, GPIO_INPUT);
	if (result != 0) {
		return result;
	}
	pressed = gpio_pin_get_dt(&boton_test_button);
	if (pressed < 0) {
		return pressed;
	}

	*initially_pressed = pressed != 0;
	return 0;
}

int kfsw_boton_test_gpio_start(void)
{
	int result;

	gpio_init_callback(&boton_test_gpio_callback, boton_test_gpio_edge,
			   BIT(boton_test_button.pin));
	result = gpio_add_callback(boton_test_button.port, &boton_test_gpio_callback);
	if (result != 0) {
		return result;
	}
	result = gpio_pin_interrupt_configure_dt(&boton_test_button, GPIO_INT_EDGE_BOTH);
	if (result != 0) {
		(void)gpio_remove_callback(boton_test_button.port, &boton_test_gpio_callback);
		return result;
	}

	/* Reconcile any transition between the initial sample and interrupt enable. */
	(void)k_work_reschedule(&boton_test_debounce_work,
				K_MSEC(CONFIG_KFSW_BOTON_TEST_DEBOUNCE_MS));
	return 0;
}
