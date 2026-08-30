#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <kfsw/modules/radio_uhf.h>

ZTEST(radio_uhf, test_holybro_identity_and_expected_configuration)
{
	struct kfsw_radio_uhf_info info;

	zassert_ok(kfsw_radio_uhf_get_info(&info));
	zassert_equal(strcmp(info.implementation, "holybro-sik"), 0);
	zassert_equal(strcmp(info.expected_hardware, "RFD SiK 2.0 on HM-TRP"), 0);
	zassert_equal(info.expected_serial_baud, 57600U);
	zassert_false(info.expected_hardware_flow_control);
}

ZTEST(radio_uhf, test_unmeasured_hardware_state_remains_unknown)
{
	struct kfsw_radio_uhf_info info;

	zassert_ok(kfsw_radio_uhf_get_info(&info));
	zassert_false(info.hardware_status_available);
	zassert_equal(info.link_state, KFSW_RADIO_UHF_LINK_UNKNOWN);
	zassert_equal(strcmp(kfsw_radio_uhf_link_state_name(info.link_state), "unknown"), 0);
}

ZTEST(radio_uhf, test_null_status_is_rejected)
{
	zassert_equal(kfsw_radio_uhf_get_info(NULL), -EINVAL);
}

ZTEST_SUITE(radio_uhf, NULL, NULL, NULL, NULL, NULL);
