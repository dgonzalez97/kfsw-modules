#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include "radio_crypto_internal.h"

#define SETTINGS_PATH "/kfsw/radio-uhf.key"
#define TEMP_PATH SETTINGS_PATH ".tmp"
#define SETTINGS_BYTES 44U

void radio_crypto_clear(void *data, size_t size)
{
	volatile uint8_t *bytes = data;

	while (size-- != 0U) {
		*bytes++ = 0;
	}
}

int radio_crypto_store_load(struct radio_crypto_settings *settings)
{
	struct fs_file_t file;
	uint8_t data[SETTINGS_BYTES + 1U];

	fs_file_t_init(&file);
	int result = fs_open(&file, SETTINGS_PATH, FS_O_READ);

	if (result != 0) {
		return result;
	}
	ssize_t bytes = fs_read(&file, data, sizeof(data));
	int closed = fs_close(&file);

	if (bytes != SETTINGS_BYTES || closed != 0 || memcmp(data, "KRU1", 4) != 0 ||
	    crc32_ieee(data, 40) != sys_get_be32(data + 40) || (data[4] & ~15U) != 0U) {
		result = -EBADMSG;
	} else {
		*settings = (struct radio_crypto_settings){.enabled = (data[4] & 1U) != 0U,
							   .tx = (data[4] & 2U) != 0U,
							   .rx = (data[4] & 4U) != 0U,
							   .key_set = (data[4] & 8U) != 0U};
		memcpy(settings->key, data + 8, sizeof(settings->key));
	}
	radio_crypto_clear(data, sizeof(data));
	return result;
}

int radio_crypto_store_save(const struct radio_crypto_settings *settings)
{
	struct fs_file_t file;
	uint8_t data[SETTINGS_BYTES] = {'K', 'R', 'U', '1'};

	data[4] = (settings->enabled ? 1U : 0U) | (settings->tx ? 2U : 0U) |
		  (settings->rx ? 4U : 0U) | (settings->key_set ? 8U : 0U);
	memcpy(data + 8, settings->key, sizeof(settings->key));
	sys_put_be32(crc32_ieee(data, 40), data + 40);
	fs_file_t_init(&file);
	int result = fs_open(&file, TEMP_PATH, FS_O_CREATE | FS_O_TRUNC | FS_O_WRITE);

	if (result == 0) {
		ssize_t written = fs_write(&file, data, sizeof(data));

		result = written == sizeof(data) ? fs_sync(&file) : -EIO;
		int closed = fs_close(&file);

		if (result == 0) {
			result = closed;
		}
		if (result == 0) {
			result = fs_rename(TEMP_PATH, SETTINGS_PATH);
		}
	}
	radio_crypto_clear(data, sizeof(data));
	return result;
}
