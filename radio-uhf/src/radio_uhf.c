#include <errno.h>

#if CONFIG_KFSW_RADIO_UHF_SHELL
#include <zephyr/shell/shell.h>
#endif
#include <zephyr/sys/util.h>

#include <kfsw/modules/radio_uhf.h>

#include "radio_uhf_internal.h"

#if CONFIG_KFSW_RADIO_UHF_HOLYBRO
#define KFSW_RADIO_UHF_SELECTED_IMPLEMENTATION kfsw_radio_uhf_holybro
#else
#error "No K-FSW UHF radio implementation selected"
#endif

int kfsw_radio_uhf_get_info(struct kfsw_radio_uhf_info *info)
{
	const struct kfsw_radio_uhf_implementation *implementation =
		&KFSW_RADIO_UHF_SELECTED_IMPLEMENTATION;

	if (info == NULL) {
		return -EINVAL;
	}

	*info = (struct kfsw_radio_uhf_info){
		.implementation = implementation->name,
		.expected_hardware = implementation->expected_hardware,
		.expected_serial_baud = implementation->expected_serial_baud,
		.expected_hardware_flow_control = implementation->expected_hardware_flow_control,
		.hardware_status_available = false,
		.link_state = KFSW_RADIO_UHF_LINK_UNKNOWN,
	};
	return 0;
}

const char *kfsw_radio_uhf_link_state_name(enum kfsw_radio_uhf_link_state state)
{
	switch (state) {
	case KFSW_RADIO_UHF_LINK_UNKNOWN:
	default:
		return "unknown";
	}
}

#if CONFIG_KFSW_RADIO_UHF_SHELL
static int cmd_uhf_status(const struct shell *sh, size_t argc, char **argv)
{
	struct kfsw_radio_uhf_info info;
	int result;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	result = kfsw_radio_uhf_get_info(&info);
	if (result != 0) {
		shell_error(sh, "UHF status unavailable (%d)", result);
		return result;
	}

	shell_print(sh, "UHF radio");
	shell_print(sh, "enabled: yes");
	shell_print(sh, "implementation: %s", info.implementation);
	shell_print(sh, "expected hardware: %s", info.expected_hardware);
	shell_print(sh, "configuration source: build-time expectation, not hardware readback");
	shell_print(sh, "expected serial: %u 8N1", info.expected_serial_baud);
	shell_print(sh, "expected flow control: %s",
		    info.expected_hardware_flow_control ? "hardware" : "none");
	shell_print(sh, "hardware status: %s",
		    info.hardware_status_available ? "available" : "unavailable");
	shell_print(sh, "RF link: %s", kfsw_radio_uhf_link_state_name(info.link_state));

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(uhf_commands,
	SHELL_CMD_ARG(status, NULL, "Show configured UHF radio identity and status.",
		      cmd_uhf_status, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(uhf, &uhf_commands, "K-FSW UHF radio diagnostics.", NULL);
#endif
