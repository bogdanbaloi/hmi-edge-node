/**
 * @file adxl_port.c
 * @brief The accelerometer's bus on this board: I2C1 at address 0x53.
 */

#include "adxl_port.h"

#include "i2c.h"

#include <stddef.h>

/**
 * The address lives here rather than in the driver.
 *
 * SDO/ALT ADDRESS is grounded on this wiring, which the datasheet says
 * gives 0x53. Tie that pin high instead and it becomes 0x1D, and the only
 * file that would have to change is this one.
 */
static const uint8_t k_device_address = ADXL345_ADDRESS_ALT_LOW;

/// Translate the driver's zero-or-not contract into the I2C one.
static int port_write(void *ctx, const uint8_t *bytes, uint8_t count) {
    const uint8_t address = *(const uint8_t *)ctx;
    return (i2c1_write(address, bytes, count) == I2C_OK) ? 0 : -1;
}

static int port_write_then_read(void *ctx, const uint8_t *tx, uint8_t tx_count,
                                uint8_t *rx, uint8_t rx_count) {
    const uint8_t address = *(const uint8_t *)ctx;
    return (i2c1_write_then_read(address, tx, tx_count, rx, rx_count) == I2C_OK)
               ? 0
               : -1;
}

static const adxl345_bus_t k_bus = {
    .ctx = (void *)&k_device_address,
    .write = port_write,
    .write_then_read = port_write_then_read,
};

const adxl345_bus_t *adxl_bus(void) {
    return &k_bus;
}

/* --- the same device, as the stream sees it ----------------------------- */

static int stream_fifo_level(void *ctx, uint8_t *out_entries) {
    (void)ctx;
    return (adxl345_fifo_level(&k_bus, out_entries) == ADXL345_OK) ? 0 : -1;
}

/// The driver hands back a sample struct while the stream asks for three
/// numbers, so the unpacking happens here. That is the whole reason this
/// adapter exists: neither side has to know the other's shape.
static int stream_read_sample(void *ctx, int16_t *x, int16_t *y, int16_t *z) {
    (void)ctx;
    adxl345_sample_t sample = {0, 0, 0};
    if (adxl345_read_sample(&k_bus, &sample) != ADXL345_OK) {
        return -1;
    }
    *x = sample.x;
    *y = sample.y;
    *z = sample.z;
    return 0;
}

static const vib_sensor_t k_stream = {
    .ctx = NULL,
    .fifo_level = stream_fifo_level,
    .read_sample = stream_read_sample,
};

const vib_sensor_t *adxl_stream(void) {
    return &k_stream;
}
