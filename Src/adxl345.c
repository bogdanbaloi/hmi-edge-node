/**
 * @file adxl345.c
 * @brief Register numbers and byte order for the ADXL345. No registers of
 *        our own are touched: the bus arrives as function pointers.
 */

#include "adxl345.h"

#include <stddef.h>

/* ---- Registers, Table 16 of the datasheet ------------------------------ */
#define REG_DEVID         0x00U
#define REG_BW_RATE       0x2CU
#define REG_POWER_CTL     0x2DU
#define REG_DATA_FORMAT   0x31U
#define REG_DATAX0        0x32U
#define REG_FIFO_CTL      0x38U
#define REG_FIFO_STATUS   0x39U

/* ---- The values written at start-up ------------------------------------
 * BW_RATE: rate code 0b1101 is 800 Hz with a 400 Hz bandwidth, Table 6.
 * The LOW_POWER bit stays clear, since low power only helps between
 * 12.5 Hz and 400 Hz and costs noise.
 */
#define BW_RATE_800_HZ    0x0DU

/* DATA_FORMAT: FULL_RES on, range 0b01 for plus or minus 4 g. In full
 * resolution the scale stays at 4 mg per count whatever the range is, so
 * widening the range later buys headroom without rescaling anything. */
#define DATA_FORMAT_FULL_RES  (1U << 3)
#define DATA_FORMAT_RANGE_4G  0x01U
#define DATA_FORMAT_VALUE     (DATA_FORMAT_FULL_RES | DATA_FORMAT_RANGE_4G)

/* FIFO_CTL: mode 0b10 is stream, Table 19. The sensor keeps the latest 32
 * samples and overwrites the oldest, so the firmware never has to arrive
 * on time, only often enough. The samples field is the watermark, unused
 * here because nothing reads the interrupt pins. */
#define FIFO_CTL_MODE_STREAM  (2U << 6)

/* POWER_CTL: the measure bit. The part powers up in standby. */
#define POWER_CTL_MEASURE     (1U << 3)

/// FIFO_STATUS bits 5:0 are the count. Bit 7 is FIFO_TRIG, not part of it.
#define FIFO_STATUS_ENTRIES_MASK  0x3FU

/// One sample is six bytes, X then Y then Z, low byte first.
#define SAMPLE_BYTES          6U
#define SAMPLE_X_LOW          0U
#define SAMPLE_X_HIGH         1U
#define SAMPLE_Y_LOW          2U
#define SAMPLE_Y_HIGH         3U
#define SAMPLE_Z_LOW          4U
#define SAMPLE_Z_HIGH         5U

#define HIGH_BYTE_SHIFT       8U

/// A register number plus the byte to put in it.
#define WRITE_PAIR_BYTES      2U

static int bus_usable(const adxl345_bus_t *bus) {
    return (bus != NULL) && (bus->write != NULL) && (bus->write_then_read != NULL);
}

/// Write one register. Returns zero on success, like the injected bus does.
static int write_register(const adxl345_bus_t *bus, uint8_t reg, uint8_t value) {
    const uint8_t pair[WRITE_PAIR_BYTES] = { reg, value };
    return bus->write(bus->ctx, pair, WRITE_PAIR_BYTES);
}

/// Read `count` bytes starting at `reg`, in one transfer.
static int read_registers(const adxl345_bus_t *bus, uint8_t reg, uint8_t *out,
                          uint8_t count) {
    return bus->write_then_read(bus->ctx, &reg, 1U, out, count);
}

/// Two bytes, low first, as a signed 16-bit value in two's complement.
static int16_t to_signed(uint8_t low, uint8_t high) {
    const uint16_t raw = (uint16_t)((uint16_t)low
                                    | ((uint16_t)high << HIGH_BYTE_SHIFT));
    return (int16_t)raw;
}

adxl345_result_t adxl345_begin(const adxl345_bus_t *bus) {
    if (!bus_usable(bus)) {
        return ADXL345_BUS_ERROR;
    }

    uint8_t device_id = 0U;
    if (read_registers(bus, REG_DEVID, &device_id, 1U) != 0) {
        return ADXL345_BUS_ERROR;
    }
    if (device_id != ADXL345_DEVICE_ID) {
        /* Something answered on this address. Configuring it would be
         * writing into a part whose registers mean something else. */
        return ADXL345_WRONG_DEVICE;
    }

    /* Everything below happens while the part is still in standby. */
    if (write_register(bus, REG_BW_RATE, BW_RATE_800_HZ) != 0) {
        return ADXL345_BUS_ERROR;
    }
    if (write_register(bus, REG_DATA_FORMAT, DATA_FORMAT_VALUE) != 0) {
        return ADXL345_BUS_ERROR;
    }
    if (write_register(bus, REG_FIFO_CTL, FIFO_CTL_MODE_STREAM) != 0) {
        return ADXL345_BUS_ERROR;
    }
    /* Measurement last, so the first sample is taken with every setting
     * already in place rather than with the reset ones. */
    if (write_register(bus, REG_POWER_CTL, POWER_CTL_MEASURE) != 0) {
        return ADXL345_BUS_ERROR;
    }
    return ADXL345_OK;
}

adxl345_result_t adxl345_fifo_level(const adxl345_bus_t *bus,
                                    uint8_t *out_entries) {
    if (!bus_usable(bus) || (out_entries == NULL)) {
        return ADXL345_BUS_ERROR;
    }

    uint8_t status = 0U;
    if (read_registers(bus, REG_FIFO_STATUS, &status, 1U) != 0) {
        return ADXL345_BUS_ERROR;
    }
    *out_entries = (uint8_t)(status & FIFO_STATUS_ENTRIES_MASK);
    return ADXL345_OK;
}

adxl345_result_t adxl345_read_sample(const adxl345_bus_t *bus,
                                     adxl345_sample_t *out) {
    if (!bus_usable(bus) || (out == NULL)) {
        return ADXL345_BUS_ERROR;
    }

    uint8_t raw[SAMPLE_BYTES] = { 0U };
    if (read_registers(bus, REG_DATAX0, raw, SAMPLE_BYTES) != 0) {
        return ADXL345_BUS_ERROR;
    }

    out->x = to_signed(raw[SAMPLE_X_LOW], raw[SAMPLE_X_HIGH]);
    out->y = to_signed(raw[SAMPLE_Y_LOW], raw[SAMPLE_Y_HIGH]);
    out->z = to_signed(raw[SAMPLE_Z_LOW], raw[SAMPLE_Z_HIGH]);
    return ADXL345_OK;
}
