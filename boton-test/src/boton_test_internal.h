#ifndef KFSW_MODULES_BOTON_TEST_INTERNAL_H
#define KFSW_MODULES_BOTON_TEST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <kfsw/modules/boton_test.h>

void kfsw_boton_test_state_reset(bool initially_pressed);
void kfsw_boton_test_process_level(bool pressed, uint64_t monotonic_ms);

#if CONFIG_ZTEST
void kfsw_boton_test_test_disable(void);
void kfsw_boton_test_test_set_press_count(uint32_t press_count);
#endif

#if CONFIG_KFSW_BOTON_TEST_GPIO
int kfsw_boton_test_gpio_prepare(bool *initially_pressed);
int kfsw_boton_test_gpio_start(void);
#endif

#if CONFIG_KFSW_BOTON_TEST_LED_GPIO
int kfsw_boton_test_led_gpio_prepare(void);
int kfsw_boton_test_led_gpio_set(enum kfsw_boton_test_led led, bool on);
#endif

#endif
