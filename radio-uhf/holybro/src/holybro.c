#include "radio_uhf_internal.h"

const struct kfsw_radio_uhf_implementation kfsw_radio_uhf_holybro = {
	.name = "holybro-sik",
	.expected_hardware = "RFD SiK 2.0 on HM-TRP",
	.expected_serial_baud = CONFIG_KFSW_RADIO_UHF_EXPECTED_SERIAL_BAUD,
	.expected_hardware_flow_control = false,
};
