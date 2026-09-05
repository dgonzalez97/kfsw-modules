#include <stdint.h>

#include <zephyr/sys/util.h>

#include <kfsw/modules/radio_uhf.h>
#include <kfsw/services/parameter.h>

/*
 * Everything the module knows, published so an operator can confirm what a node
 * expects of its radio without reading the build.
 *
 * Read-only throughout, and deliberately honest about what these values are:
 * the module selects an implementation at build time and does not configure or
 * interrogate the modem. link_state stays unknown unless the implementation can
 * actually read it back, because reporting a link as up on no evidence is worse
 * than admitting it is not known.
 */

#define KFSW_RADIO_UHF_NAME_SIZE 24U

static char radio_implementation[KFSW_RADIO_UHF_NAME_SIZE];
static char radio_expected_hardware[KFSW_RADIO_UHF_NAME_SIZE];
static uint32_t radio_expected_baud;
static uint8_t radio_expected_flow_control;
static uint8_t radio_status_available;
static uint8_t radio_link_state;

static void copy_name(char *destination, const char *source)
{
	size_t length = 0U;

	while ((length + 1U < KFSW_RADIO_UHF_NAME_SIZE) && (source != NULL) &&
	       (source[length] != '\0')) {
		destination[length] = source[length];
		length++;
	}
	destination[length] = '\0';
}

static void sample_info(void)
{
	struct kfsw_radio_uhf_info info;

	if (kfsw_radio_uhf_get_info(&info) != 0) {
		return;
	}
	copy_name(radio_implementation, info.implementation);
	copy_name(radio_expected_hardware, info.expected_hardware);
	radio_expected_baud = info.expected_serial_baud;
	radio_expected_flow_control = info.expected_hardware_flow_control ? 1U : 0U;
	radio_status_available = info.hardware_status_available ? 1U : 0U;
	radio_link_state = (uint8_t)info.link_state;
}

static void sample_implementation(void *value)
{
	sample_info();
	copy_name(value, radio_implementation);
}

static void sample_expected_hardware(void *value)
{
	sample_info();
	copy_name(value, radio_expected_hardware);
}

#define RADIO_SAMPLE(field, type)                                                                  \
	static void sample_##field(void *value)                                                    \
	{                                                                                          \
		sample_info();                                                                     \
		*(type *)value = radio_##field;                                                    \
	}

RADIO_SAMPLE(expected_baud, uint32_t)
RADIO_SAMPLE(expected_flow_control, uint8_t)
RADIO_SAMPLE(status_available, uint8_t)
RADIO_SAMPLE(link_state, uint8_t)

static const struct kfsw_param_definition radio_uhf_param_definitions[] = {
	{
		.offset = 0x00U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_RADIO_UHF_NAME_SIZE,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "uhf_implementation",
		.description = "Radio implementation this build selected",
		.value = radio_implementation,
		.sample = sample_implementation,
	},
	{
		.offset = 0x20U,
		.type = KFSW_PARAM_STRING,
		.capacity = KFSW_RADIO_UHF_NAME_SIZE,
		.flags = KFSW_PARAM_FLAG_READ_ONLY | KFSW_PARAM_FLAG_SYSTEM_INFO,
		.name = "uhf_expected_hardware",
		.description = "Hardware this composition expects; not a live readback",
		.value = radio_expected_hardware,
		.sample = sample_expected_hardware,
	},
	{
		.offset = 0x40U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_expected_baud",
		.unit = "baud",
		.description = "Serial rate the radio profile expects; not applied here",
		.value = &radio_expected_baud,
		.sample = sample_expected_baud,
	},
	{
		.offset = 0x44U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_expected_flow",
		.description = "Whether the profile expects hardware flow control",
		.value = &radio_expected_flow_control,
		.sample = sample_expected_flow_control,
	},
	{
		.offset = 0x45U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_status_available",
		.description = "Whether this implementation reads the modem back at all",
		.value = &radio_status_available,
		.sample = sample_status_available,
	},
	{
		.offset = 0x46U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_link_state",
		/* Unknown is a real value, not a placeholder: without a readback
		 * there is no evidence either way, and reporting a link as up on
		 * no evidence is the reading that gets acted on wrongly. */
		.description = "unknown, down or up; unknown unless the modem is read back",
		.value = &radio_link_state,
		.sample = sample_link_state,
	},
};

const struct kfsw_param_definition_set kfsw_radio_uhf_param_definitions = {
	.table = KFSW_RADIO_UHF_PARAM_TABLE_ID,
	.name = KFSW_RADIO_UHF_PARAM_TABLE_NAME,
	.definitions = radio_uhf_param_definitions,
	.count = ARRAY_SIZE(radio_uhf_param_definitions),
};
