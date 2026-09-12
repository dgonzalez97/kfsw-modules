#include <stdint.h>
#include <string.h>
#if CONFIG_KFSW_RADIO_UHF_CRYPTO
#include "radio_crypto_internal.h"
#endif

#include <zephyr/sys/util.h>

#include <kfsw/modules/radio_uhf.h>
#include <kfsw/services/parameter.h>

/*
 * Everything the module knows, published so an operator can confirm what a node
 * expects of its radio without reading the build.
 *
 * Read-only throughout: the module picks an implementation at build time and
 * neither configures nor interrogates the modem. link_state stays unknown
 * unless it can be read back, because claiming a link is up on no evidence is
 * worse than admitting it is not known.
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

#if CONFIG_KFSW_RADIO_UHF_CRYPTO
static char key_hex[65];
static struct kfsw_radio_crypto_info crypto_info;
static uint8_t crypto_enable = 1, crypto_tx = 1, crypto_rx = 1;
static uint8_t crypto_key_set, crypto_tx_ready, crypto_rx_ready;
static uint32_t crypto_authenticated, crypto_rejected, crypto_replays;
static int32_t crypto_error;

static void key_changed(const char *text)
{
	radio_crypto_set_key(text);
	radio_crypto_clear(key_hex, sizeof(key_hex));
}

#define CRYPTO_SAMPLE(name, field, type)                                                           \
	static void sample_crypto_##name(void *value)                                              \
	{                                                                                          \
		kfsw_radio_uhf_crypto_get(&crypto_info);                                           \
		*(type *)value = (type)crypto_info.field;                                          \
	}

CRYPTO_SAMPLE(enable, enabled, uint8_t)
CRYPTO_SAMPLE(tx, encrypt_tx, uint8_t)
CRYPTO_SAMPLE(rx, encrypt_rx, uint8_t)
CRYPTO_SAMPLE(key_set, key_set, uint8_t)
CRYPTO_SAMPLE(tx_ready, tx_ready, uint8_t)
CRYPTO_SAMPLE(rx_ready, rx_ready, uint8_t)
CRYPTO_SAMPLE(authenticated, authenticated, uint32_t)
CRYPTO_SAMPLE(rejected, rejected, uint32_t)
CRYPTO_SAMPLE(replays, replays, uint32_t)
CRYPTO_SAMPLE(error, last_error, int32_t)
#endif

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
#if CONFIG_KFSW_RADIO_UHF_CRYPTO
	{
		.offset = 0x50U,
		.type = KFSW_PARAM_STRING,
		.capacity = sizeof(key_hex),
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_LOCAL_ONLY,
		.name = "uhf_key_hex",
		.description = "Local 64-digit hex key; reads return empty",
		.value = key_hex,
		.validate_text = radio_crypto_validate_key,
		.changed_text = key_changed,
	},
	{
		.offset = 0x98U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_LOCAL_ONLY,
		.name = "uhf_encrypt_enable",
		.description = "Enable radio packet protection",
		.value = &crypto_enable,
		.default_value.u8 = 1,
		.validate = radio_crypto_validate_switch,
		.changed = radio_crypto_set_enable,
		.sample = sample_crypto_enable,
	},
	{
		.offset = 0x99U,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_LOCAL_ONLY,
		.name = "uhf_encrypt_tx",
		.description = "Encrypt transmitted radio packets",
		.value = &crypto_tx,
		.default_value.u8 = 1,
		.validate = radio_crypto_validate_switch,
		.changed = radio_crypto_set_tx,
		.sample = sample_crypto_tx,
	},
	{
		.offset = 0x9aU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_CONFIGURATION | KFSW_PARAM_FLAG_LOCAL_ONLY,
		.name = "uhf_encrypt_rx",
		.description = "Require authenticated encrypted radio packets",
		.value = &crypto_rx,
		.default_value.u8 = 1,
		.validate = radio_crypto_validate_switch,
		.changed = radio_crypto_set_rx,
		.sample = sample_crypto_rx,
	},
	{
		.offset = 0x9bU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_key_set",
		.value = &crypto_key_set,
		.sample = sample_crypto_key_set,
	},
	{
		.offset = 0x9cU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_tx_ready",
		.value = &crypto_tx_ready,
		.sample = sample_crypto_tx_ready,
	},
	{
		.offset = 0x9dU,
		.type = KFSW_PARAM_U8,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_rx_ready",
		.value = &crypto_rx_ready,
		.sample = sample_crypto_rx_ready,
	},
	{
		.offset = 0xa0U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_authenticated",
		.value = &crypto_authenticated,
		.sample = sample_crypto_authenticated,
	},
	{
		.offset = 0xa4U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_rejected",
		.value = &crypto_rejected,
		.sample = sample_crypto_rejected,
	},
	{
		.offset = 0xa8U,
		.type = KFSW_PARAM_U32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_replays",
		.value = &crypto_replays,
		.sample = sample_crypto_replays,
	},
	{
		.offset = 0xacU,
		.type = KFSW_PARAM_I32,
		.flags = KFSW_PARAM_FLAG_READ_ONLY,
		.name = "uhf_crypto_error",
		.value = &crypto_error,
		.sample = sample_crypto_error,
	},
#endif

};

const struct kfsw_param_definition_set kfsw_radio_uhf_param_definitions = {
	.table = KFSW_RADIO_UHF_PARAM_TABLE_ID,
	.name = KFSW_RADIO_UHF_PARAM_TABLE_NAME,
	.definitions = radio_uhf_param_definitions,
	.count = ARRAY_SIZE(radio_uhf_param_definitions),
};
