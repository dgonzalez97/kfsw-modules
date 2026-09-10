#ifndef KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_INTERNAL_H
#define KFSW_MODULES_TEMPERATURE_SENSOR_EXAMPLE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <kfsw/modules/temperature_sensor_example.h>

/*
 * The cache and the sensor are separate files so the cache can be tested on a
 * board that has no die temperature, which is every board Twister runs on.
 */
void kfsw_temp_example_store(int32_t milli_c, uint32_t monotonic_ms);
void kfsw_temp_example_store_failure(void);

#if CONFIG_KFSW_TEMP_EXAMPLE_SENSOR
int kfsw_temp_example_sensor_prepare(void);
int kfsw_temp_example_sensor_read(int32_t *milli_c);
#endif

#endif
