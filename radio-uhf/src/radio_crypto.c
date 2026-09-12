#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <psa/crypto.h>
#include <kfsw/comms/uart_codec.h>
#include <kfsw/modules/radio_uhf.h>

#include "radio_crypto_internal.h"

#define LOCAL CONFIG_KFSW_CSP_ADDRESS
#define PEER CONFIG_KFSW_CSP_UART_PEER_ADDRESS
#define NONCE_BYTES 16U
#define MAC_BYTES 32U
#define TAG_BYTES 16U
#define DATA_HEADER 12U
#define HELLO_BYTES 68U
#define REPLY_BYTES 84U
#define AAD_BYTES 8U
#define RETRY_MS 1000
#define HMAC_ALG PSA_ALG_HMAC(PSA_ALG_SHA_256)

enum frame_type { DATA, HELLO, REPLY };

#if CONFIG_BOARD_NATIVE_SIM
int kfsw_radio_host_random(void *data, size_t size);
static int radio_random(uint8_t *data, size_t size)
{
	return kfsw_radio_host_random(data, size);
}
#else
BUILD_ASSERT(IS_ENABLED(CONFIG_CSPRNG_ENABLED) && !IS_ENABLED(CONFIG_TEST_CSPRNG_GENERATOR),
	     "Radio sessions require a secure entropy source");
static int radio_random(uint8_t *data, size_t size)
{
	return psa_generate_random(data, size) == PSA_SUCCESS ? 0 : -EIO;
}
#endif

static K_MUTEX_DEFINE(crypto_lock);
static struct radio_crypto_settings settings = {.enabled = true, .tx = true, .rx = true};
static struct kfsw_radio_crypto_info status;
static psa_key_id_t master, tx_key, rx_key;
static uint8_t boot_nonce[NONCE_BYTES], request_nonce[NONCE_BYTES];
static uint8_t peer_boot[NONCE_BYTES], peer_request[NONCE_BYTES], server_nonce[NONCE_BYTES];
static uint8_t scratch[CSP_BUFFER_SIZE];
static uint64_t tx_sequence, rx_sequence;
static int64_t last_request = -RETRY_MS, last_hello = -RETRY_MS;
static bool initialized, pending;

static void clear_sessions(void)
{
	psa_destroy_key(tx_key);
	psa_destroy_key(rx_key);
	tx_key = rx_key = 0;
	tx_sequence = rx_sequence = 0;
	pending = false;
	last_request = last_hello = -RETRY_MS;
}

static int import_master(void)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_key_id_t key = 0;

	clear_sessions();
	psa_destroy_key(master);
	master = 0;
	psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attributes, 256);
	psa_set_key_usage_flags(&attributes,
				PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
	psa_set_key_algorithm(&attributes, HMAC_ALG);
	psa_status_t result = psa_import_key(&attributes, settings.key, sizeof(settings.key), &key);

	psa_reset_key_attributes(&attributes);
	if (result != PSA_SUCCESS) {
		return -EIO;
	}
	master = key;
	return 0;
}

static void put_header(uint8_t *out, const csp_id_t *id)
{
	sys_put_be16(id->src, out);
	sys_put_be16(id->dst, out + 2);
	out[4] = id->sport;
	out[5] = id->dport;
	out[6] = id->pri;
	out[7] = id->flags;
}

static int sign_control(uint8_t *data, size_t size, bool verify)
{
	uint8_t input[AAD_BYTES + REPLY_BYTES - MAC_BYTES];
	csp_id_t id = {.src = verify ? PEER : LOCAL, .dst = verify ? LOCAL : PEER};
	size_t written = 0;
	psa_status_t result;

	put_header(input, &id);
	memcpy(input + AAD_BYTES, data, size - MAC_BYTES);
	if (verify) {
		result = psa_mac_verify(master, HMAC_ALG, input, AAD_BYTES + size - MAC_BYTES,
					data + size - MAC_BYTES, MAC_BYTES);
	} else {
		result = psa_mac_compute(master, HMAC_ALG, input, AAD_BYTES + size - MAC_BYTES,
					 data + size - MAC_BYTES, MAC_BYTES, &written);
	}
	return result == PSA_SUCCESS ? 0 : -EACCES;
}

/* The master key is uniformly random key material, not a human password.
 * Bind each directional key to both endpoints and fresh handshake challenges.
 */
static int derive_key(const uint8_t *client_boot, const uint8_t *request, const uint8_t *server,
		      bool transmit)
{
	static const uint8_t label[] = "KFSW UHF session v1";
	uint8_t context[sizeof(label) + 4U + 3U * NONCE_BYTES];
	uint8_t material[32];
	size_t written;
	psa_key_id_t key = 0;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

	memcpy(context, label, sizeof(label));
	sys_put_be16(transmit ? LOCAL : PEER, context + sizeof(label));
	sys_put_be16(transmit ? PEER : LOCAL, context + sizeof(label) + 2U);
	memcpy(context + sizeof(label) + 4U, client_boot, NONCE_BYTES);
	memcpy(context + sizeof(label) + 4U + NONCE_BYTES, request, NONCE_BYTES);
	memcpy(context + sizeof(label) + 4U + 2U * NONCE_BYTES, server, NONCE_BYTES);
	psa_status_t result = psa_mac_compute(master, HMAC_ALG, context, sizeof(context), material,
					      sizeof(material), &written);
	if (result == PSA_SUCCESS) {
		psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
		psa_set_key_bits(&attributes, 256);
		psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
		psa_set_key_usage_flags(&attributes,
					transmit ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
		result = psa_import_key(&attributes, material, sizeof(material), &key);
	}
	psa_reset_key_attributes(&attributes);
	radio_crypto_clear(material, sizeof(material));
	if (result != PSA_SUCCESS) {
		return -EIO;
	}
	if (transmit) {
		psa_destroy_key(tx_key);
		tx_key = key;
		tx_sequence = 0;
	} else {
		psa_destroy_key(rx_key);
		rx_key = key;
		rx_sequence = 0;
	}
	return 0;
}

static void frame_header(uint8_t *data, enum frame_type type)
{
	data[0] = 'K';
	data[1] = 'R';
	data[2] = 1;
	data[3] = (uint8_t)type;
}

static int connect_locked(void)
{
	uint8_t hello[HELLO_BYTES];
	int64_t now = k_uptime_get();

	if (master == 0 || !settings.enabled) {
		return -EACCES;
	}
	if (now - last_request < RETRY_MS) {
		return -EAGAIN;
	}
	last_request = now;
	if (radio_random(request_nonce, sizeof(request_nonce)) != 0) {
		return -EIO;
	}
	frame_header(hello, HELLO);
	memcpy(hello + 4, boot_nonce, NONCE_BYTES);
	memcpy(hello + 4 + NONCE_BYTES, request_nonce, NONCE_BYTES);
	int result = sign_control(hello, sizeof(hello), false);

	if (result == 0) {
		pending = true;
		result = kfsw_uart_codec_control(PEER, hello, sizeof(hello));
	}
	return result;
}

int kfsw_radio_uhf_crypto_connect(void)
{
	k_mutex_lock(&crypto_lock, K_FOREVER);
	int result = connect_locked();
	status.last_error = result;
	k_mutex_unlock(&crypto_lock);
	return result;
}

static int hello_received(csp_packet_t *packet)
{
	uint8_t reply[REPLY_BYTES];
	uint8_t *client = packet->data + 4;
	uint8_t *request = client + NONCE_BYTES;
	bool new_boot = rx_key == 0 || memcmp(peer_boot, client, NONCE_BYTES) != 0;
	bool duplicate = !new_boot && memcmp(peer_request, request, NONCE_BYTES) == 0;
	int64_t now = k_uptime_get();

	if (!duplicate && now - last_hello < RETRY_MS) {
		return -EAGAIN;
	}
	last_hello = now;
	if (!duplicate) {
		uint8_t fresh[NONCE_BYTES];
		if (radio_random(fresh, sizeof(fresh)) != 0) {
			return -EIO;
		}
		int result = derive_key(client, request, fresh, false);
		if (result != 0) {
			return result;
		}
		memcpy(peer_boot, client, NONCE_BYTES);
		memcpy(peer_request, request, NONCE_BYTES);
		memcpy(server_nonce, fresh, NONCE_BYTES);
	}
	frame_header(reply, REPLY);
	memcpy(reply + 4, client, NONCE_BYTES);
	memcpy(reply + 4 + NONCE_BYTES, request, NONCE_BYTES);
	memcpy(reply + 4 + 2U * NONCE_BYTES, server_nonce, NONCE_BYTES);
	int result = sign_control(reply, sizeof(reply), false);
	if (result == 0) {
		result = kfsw_uart_codec_control(PEER, reply, sizeof(reply));
	}
	/* A peer restart also invalidates the old outbound session. */
	if (new_boot && !pending) {
		psa_destroy_key(tx_key);
		tx_key = 0;
		(void)connect_locked();
	}
	return result;
}

static int control_received(csp_packet_t *packet)
{
	size_t expected = packet->data[3] == HELLO ? HELLO_BYTES : REPLY_BYTES;
	if (packet->length != expected || packet->id.src != PEER || packet->id.dst != LOCAL ||
	    packet->id.sport != 0 || packet->id.dport != 0 || packet->id.flags != 0 ||
	    packet->id.pri != 0 || master == 0 || !settings.enabled ||
	    sign_control(packet->data, expected, true) != 0) {
		return -EACCES;
	}
	if (packet->data[3] == HELLO) {
		return hello_received(packet);
	}
	if (!pending || memcmp(packet->data + 4, boot_nonce, NONCE_BYTES) != 0 ||
	    memcmp(packet->data + 4 + NONCE_BYTES, request_nonce, NONCE_BYTES) != 0) {
		return -EALREADY;
	}
	int result =
		derive_key(boot_nonce, request_nonce, packet->data + 4 + 2U * NONCE_BYTES, true);
	if (result == 0) {
		pending = false;
	}
	return result;
}

static int encode(csp_packet_t *packet)
{
	uint8_t nonce[12], aad[AAD_BYTES + DATA_HEADER];
	size_t written;
	int result = 0;

	k_mutex_lock(&crypto_lock, K_FOREVER);
	if (!settings.enabled || !settings.tx) {
		goto done;
	}
	if (tx_key == 0) {
		(void)connect_locked();
		result = -EAGAIN;
		goto done;
	}
	if (packet->length > CSP_BUFFER_SIZE - DATA_HEADER - TAG_BYTES - sizeof(uint32_t)) {
		result = -EMSGSIZE;
		goto done;
	}
	if (tx_sequence == UINT64_MAX) {
		result = -EOVERFLOW;
		goto done;
	}
	++tx_sequence;
	frame_header(scratch, DATA);
	sys_put_be64(tx_sequence, scratch + 4);
	put_header(aad, &packet->id);
	memcpy(aad + AAD_BYTES, scratch, DATA_HEADER);
	sys_put_be32(LOCAL, nonce);
	sys_put_be64(tx_sequence, nonce + 4);
	if (psa_aead_encrypt(tx_key, PSA_ALG_GCM, nonce, sizeof(nonce), aad, sizeof(aad),
			     packet->data, packet->length, scratch + DATA_HEADER,
			     sizeof(scratch) - DATA_HEADER - sizeof(uint32_t),
			     &written) != PSA_SUCCESS) {
		result = -EIO;
		goto done;
	}
	memcpy(packet->data, scratch, DATA_HEADER + written);
	packet->length = (uint16_t)(DATA_HEADER + written);
done:
	status.last_error = result;
	k_mutex_unlock(&crypto_lock);
	return result;
}

static int decode(csp_packet_t *packet)
{
	uint8_t nonce[12], aad[AAD_BYTES + DATA_HEADER];
	size_t written;
	int result = 0;
	bool framed = packet->length >= 4 && packet->data[0] == 'K' && packet->data[1] == 'R';

	k_mutex_lock(&crypto_lock, K_FOREVER);
	if (framed && packet->data[2] == 1 &&
	    (packet->data[3] == HELLO || packet->data[3] == REPLY)) {
		result = control_received(packet);
		if (result == 0) {
			result = 1;
		}
		goto done;
	}
	if (!settings.enabled || !settings.rx) {
		result = framed ? -EACCES : 0;
		goto done;
	}
	if (!framed || packet->data[2] != 1 || packet->data[3] != DATA || rx_key == 0 ||
	    packet->length < DATA_HEADER + TAG_BYTES) {
		result = -EACCES;
		goto done;
	}
	uint64_t sequence = sys_get_be64(packet->data + 4);
	put_header(aad, &packet->id);
	memcpy(aad + AAD_BYTES, packet->data, DATA_HEADER);
	sys_put_be32(PEER, nonce);
	sys_put_be64(sequence, nonce + 4);
	if (psa_aead_decrypt(rx_key, PSA_ALG_GCM, nonce, sizeof(nonce), aad, sizeof(aad),
			     packet->data + DATA_HEADER, packet->length - DATA_HEADER, scratch,
			     sizeof(scratch), &written) != PSA_SUCCESS) {
		result = -EACCES;
		goto done;
	}
	if (sequence <= rx_sequence) {
		status.replays++;
		result = -EALREADY;
		goto done;
	}
	rx_sequence = sequence;
	memcpy(packet->data, scratch, written);
	packet->length = (uint16_t)written;
	status.authenticated++;
done:
	if (result < 0) {
		status.rejected++;
		status.last_error = result;
	}
	radio_crypto_clear(scratch, sizeof(scratch));
	k_mutex_unlock(&crypto_lock);
	return result;
}

static const struct kfsw_uart_codec codec = {.encode = encode, .decode = decode};

int kfsw_radio_uhf_crypto_init(void)
{
	int result = kfsw_uart_codec_register(CONFIG_KFSW_RADIO_UHF_CRYPTO_INTERFACE, &codec);
	if (result != 0) {
		return result;
	}
	k_mutex_lock(&crypto_lock, K_FOREVER);
	if (psa_crypto_init() != PSA_SUCCESS || radio_random(boot_nonce, sizeof(boot_nonce)) != 0) {
		result = -EIO;
	} else {
		result = radio_crypto_store_load(&settings);
		if (result == -ENOENT) {
			result = 0;
		}
		if (result == 0 && settings.key_set) {
			result = import_master();
		}
	}
	initialized = true;
	status.last_error = result;
	k_mutex_unlock(&crypto_lock);
	return result;
}

void kfsw_radio_uhf_crypto_get(struct kfsw_radio_crypto_info *info)
{
	if (info == NULL) {
		return;
	}
	k_mutex_lock(&crypto_lock, K_FOREVER);
	*info = status;
	info->enabled = settings.enabled;
	info->encrypt_tx = settings.tx;
	info->encrypt_rx = settings.rx;
	info->key_set = master != 0;
	info->tx_ready = tx_key != 0;
	info->rx_ready = rx_key != 0;
	k_mutex_unlock(&crypto_lock);
}

int radio_crypto_validate_switch(const union kfsw_param_scalar *value)
{
	return value->u8 <= 1U ? 0 : -EINVAL;
}

static int nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

int radio_crypto_validate_key(const char *text)
{
	if (!initialized && text[0] == '\0') {
		return 0;
	}
	if (strlen(text) != 64) {
		return -EINVAL;
	}
	for (size_t i = 0; i < 64; i++) {
		if (nibble(text[i]) < 0) {
			return -EINVAL;
		}
	}
	return 0;
}

static void save_settings(struct radio_crypto_settings *next, bool changed_key)
{
	uint8_t fresh[NONCE_BYTES];
	int result = radio_random(fresh, sizeof(fresh));

	if (result == 0) {
		result = radio_crypto_store_save(next);
	}
	if (result == 0) {
		settings = *next;
		/* The peer must refresh its outbound session after our RX state resets. */
		memcpy(boot_nonce, fresh, sizeof(boot_nonce));
		clear_sessions();
		if (changed_key) {
			result = import_master();
		}
	}
	status.last_error = result;
}

void radio_crypto_set_key(const char *text)
{
	if (!initialized || strlen(text) != 64) {
		return;
	}
	k_mutex_lock(&crypto_lock, K_FOREVER);
	struct radio_crypto_settings next = settings;
	for (size_t i = 0; i < sizeof(next.key); i++) {
		next.key[i] = (uint8_t)((nibble(text[2 * i]) << 4) | nibble(text[2 * i + 1]));
	}
	next.key_set = true;
	save_settings(&next, true);
	radio_crypto_clear(&next, sizeof(next));
	k_mutex_unlock(&crypto_lock);
}

static void set_switch(uint8_t which, bool value)
{
	if (!initialized) {
		return;
	}
	k_mutex_lock(&crypto_lock, K_FOREVER);
	struct radio_crypto_settings next = settings;
	if (which == 0) {
		next.enabled = value;
	}
	if (which == 1) {
		next.tx = value;
	}
	if (which == 2) {
		next.rx = value;
	}
	save_settings(&next, false);
	radio_crypto_clear(&next, sizeof(next));
	k_mutex_unlock(&crypto_lock);
}

void radio_crypto_set_enable(const union kfsw_param_scalar *value)
{
	set_switch(0, value->u8 != 0);
}
void radio_crypto_set_tx(const union kfsw_param_scalar *value)
{
	set_switch(1, value->u8 != 0);
}
void radio_crypto_set_rx(const union kfsw_param_scalar *value)
{
	set_switch(2, value->u8 != 0);
}
