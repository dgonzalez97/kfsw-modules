#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#include "temperature_sensor_example_internal.h"

#define KFSW_TEMP_EXAMPLE_NODE DT_CHOSEN(kfsw_die_temp)

BUILD_ASSERT(DT_NODE_EXISTS(KFSW_TEMP_EXAMPLE_NODE),
	     "kfsw,die-temp must select a temperature sensor node");

static const struct device *const temp_sensor = DEVICE_DT_GET(KFSW_TEMP_EXAMPLE_NODE);

int kfsw_temp_example_sensor_prepare(void)
{
	if (!device_is_ready(temp_sensor)) {
		return -EACCES;
	}
	return 0;
}

int kfsw_temp_example_sensor_read(int32_t *milli_c)
{
	struct sensor_value value;
	int result;

	if (milli_c == NULL) {
		return -EINVAL;
	}

	result = sensor_sample_fetch_chan(temp_sensor, SENSOR_CHAN_DIE_TEMP);
	if (result != 0) {
		return result;
	}
	result = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &value);
	if (result != 0) {
		return result;
	}

	/* val1 is whole degrees and val2 millionths, carrying the same sign, so
	 * this holds either side of zero.
	 */
	*milli_c = (value.val1 * 1000) + (value.val2 / 1000);
	return 0;
}
