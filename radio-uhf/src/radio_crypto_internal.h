#ifndef KFSW_RADIO_CRYPTO_INTERNAL_H
#define KFSW_RADIO_CRYPTO_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <kfsw/services/parameter.h>

struct radio_crypto_settings {
	uint8_t key[32];
	bool key_set;
	bool enabled;
	bool tx;
	bool rx;
};

int radio_crypto_store_load(struct radio_crypto_settings *settings);
int radio_crypto_store_save(const struct radio_crypto_settings *settings);
void radio_crypto_clear(void *data, size_t size);
int radio_crypto_validate_key(const char *text);
int radio_crypto_validate_switch(const union kfsw_param_scalar *value);
void radio_crypto_set_key(const char *text);
void radio_crypto_set_enable(const union kfsw_param_scalar *value);
void radio_crypto_set_tx(const union kfsw_param_scalar *value);
void radio_crypto_set_rx(const union kfsw_param_scalar *value);

#endif
