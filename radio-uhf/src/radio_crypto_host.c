#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/random.h>

/* Built with host headers, not Zephyr's libc. native_sim's default entropy
 * driver is a repeatable test generator.
 */
int kfsw_radio_host_random(void *data, size_t size)
{
	uint8_t *out = data;
	while (size != 0U) {
		ssize_t count = getrandom(out, size, 0);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return -1;
		}
		out += count;
		size -= (size_t)count;
	}
	return 0;
}
