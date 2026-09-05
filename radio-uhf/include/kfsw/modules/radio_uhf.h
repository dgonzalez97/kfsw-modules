#ifndef KFSW_MODULES_RADIO_UHF_H
#define KFSW_MODULES_RADIO_UHF_H

#include <stdbool.h>
#include <stdint.h>

#if CONFIG_KFSW_PARAM
#include <kfsw/services/parameter.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** RF-link state observable by the current module implementation. */
enum kfsw_radio_uhf_link_state {
	/** The module does not query or otherwise know the physical RF-link state. */
	KFSW_RADIO_UHF_LINK_UNKNOWN,
};

/** Compile-time UHF-radio identity and bounded status snapshot. */
struct kfsw_radio_uhf_info {
	/** Selected implementation identifier. */
	const char *implementation;
	/** Hardware identity expected by this composition, not a live readback. */
	const char *expected_hardware;
	/** UART rate expected by the radio profile, not applied by this module. */
	uint32_t expected_serial_baud;
	/** Expected hardware flow-control setting for the selected implementation. */
	bool expected_hardware_flow_control;
	/** Whether the implementation performs live hardware status readback. */
	bool hardware_status_available;
	/** Current RF-link state, unknown when hardware status is unavailable. */
	enum kfsw_radio_uhf_link_state link_state;
};

/**
 * Copy the selected implementation's identity and status.
 *
 * This operation never enters modem command mode, changes radio settings, or
 * accesses the CSP/KISS UART data path.
 *
 * @param info Destination status structure.
 * @return 0 on success or -EINVAL when @p info is NULL.
 */
int kfsw_radio_uhf_get_info(struct kfsw_radio_uhf_info *info);

/** Return a stable printable label for a UHF RF-link state. */
const char *kfsw_radio_uhf_link_state_name(enum kfsw_radio_uhf_link_state state);

#if CONFIG_KFSW_PARAM
/** Parameter table owned by this module, in the module band. */
#define KFSW_RADIO_UHF_PARAM_TABLE_ID 50U
/** Stable logical name paired with KFSW_RADIO_UHF_PARAM_TABLE_ID. */
#define KFSW_RADIO_UHF_PARAM_TABLE_NAME "radio_uhf"

/** What this composition expects of its radio, and what it can see of it. */
extern const struct kfsw_param_definition_set kfsw_radio_uhf_param_definitions;
#endif

#ifdef __cplusplus
}
#endif

#endif
