#ifndef KFSW_MODULES_BOTON_TEST_H
#define KFSW_MODULES_BOTON_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct kfsw_param_definition_set;

/** @defgroup kfsw_modules_boton_test boton_test Module
 *  @ingroup kfsw_modules
 *  Debounced button ownership, live status, and parameter definitions.
 *
 *  @{
 */

/** Consistent snapshot of volatile button state since the current boot. */
struct kfsw_boton_test_status {
	/** Valid debounced presses; saturates at UINT32_MAX. */
	uint32_t press_count;
	/** Monotonic whole seconds of the latest press, or zero before a press. */
	uint32_t last_press_s;
};

/**
 * Initialize the module and its optional physical GPIO composition.
 *
 * Runtime state starts at zero on each boot. If the button is already held at
 * initialization, it is treated as held rather than as a new press; a release
 * followed by another press is required before the count changes.
 *
 * @return 0 on success, or a negative errno value when GPIO setup fails.
 */
int kfsw_boton_test_init(void);

/**
 * Copy a consistent snapshot of the module-owned runtime state.
 *
 * @param status Destination status structure.
 * @return 0 on success, -EINVAL for a NULL destination, or -EACCES before
 *         module initialization.
 */
int kfsw_boton_test_get_status(struct kfsw_boton_test_status *status);

/** Read-only, non-persistent live PARAM definitions owned by boton_test. */
extern const struct kfsw_param_definition_set kfsw_boton_test_param_definitions;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
