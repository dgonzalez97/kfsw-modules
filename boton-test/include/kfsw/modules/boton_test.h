#ifndef KFSW_MODULES_BOTON_TEST_H
#define KFSW_MODULES_BOTON_TEST_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kfsw_param_definition_set;

/** Parameter table of the hardware test example. */
#define KFSW_HW_TEST_TABLE_ID 67U
/** Stable logical name paired with KFSW_HW_TEST_TABLE_ID. */
#define KFSW_HW_TEST_TABLE_NAME "hw_test"

/** @defgroup kfsw_modules_boton_test boton_test Module
 *  @ingroup kfsw_modules
 *  Debounced button, LEDs, status and parameters.
 *
 *  @{
 */

/** Independently controlled LEDs in the developer hardware-test example. */
enum kfsw_boton_test_led {
	KFSW_BOTON_TEST_LED_GREEN,
	KFSW_BOTON_TEST_LED_BLUE,
	KFSW_BOTON_TEST_LED_RED,
	KFSW_BOTON_TEST_LED_COUNT,
};

/** Consistent snapshot of volatile button and LED state since the current boot. */
struct kfsw_boton_test_status {
	/** Valid debounced presses; saturates at UINT32_MAX. */
	uint32_t press_count;
	/** Monotonic whole seconds of the latest press, or zero before a press. */
	uint32_t last_press_s;
	/** Logical state of the green developer LED. */
	bool led_green;
	/** Logical state of the blue developer LED. */
	bool led_blue;
	/** Logical state of the red developer LED. */
	bool led_red;
};

/**
 * Initialize the module and its GPIO.
 *
 * State starts at zero on each boot. A button already held at startup is not
 * counted until it is released and pressed again.
 *
 * @return 0 on success, or a negative errno value when GPIO setup fails.
 */
int kfsw_boton_test_init(void);

/**
 * Copy a consistent snapshot of the module state.
 *
 * @param status Destination status structure.
 * @return 0 on success, -EINVAL for a NULL destination, or -EACCES before
 *         module initialization.
 */
int kfsw_boton_test_get_status(struct kfsw_boton_test_status *status);

/**
 * Set one LED.
 *
 * @param led LED colour to control.
 * @param on True to turn the LED on, false to turn it off.
 * @return 0 on success, -EINVAL for an unknown LED, -EACCES before module
 *         initialization, or a negative GPIO error.
 */
int kfsw_boton_test_set_led(enum kfsw_boton_test_led led, bool on);

/** Parameter table of boton_test. */
extern const struct kfsw_param_definition_set kfsw_boton_test_param_definitions;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
