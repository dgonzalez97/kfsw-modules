#ifndef KFSW_MODULES_RADIO_UHF_INTERNAL_H
#define KFSW_MODULES_RADIO_UHF_INTERNAL_H

#include <kfsw/modules/radio_uhf.h>

struct kfsw_radio_uhf_implementation {
	const char *name;
	const char *expected_hardware;
	uint32_t expected_serial_baud;
	bool expected_hardware_flow_control;
};

#if CONFIG_KFSW_RADIO_UHF_HOLYBRO
extern const struct kfsw_radio_uhf_implementation kfsw_radio_uhf_holybro;
#endif

#endif
