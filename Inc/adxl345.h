#ifndef ADXL345_H
#define ADXL345_H

#include <stdint.h>

/**
 * @file adxl345.h
 * @brief The ADXL345 accelerometer, as a pure decision layer over an
 *        injected bus. Touches no register and includes no driver.
 *
 * **Why it does not call i2c.h directly.** Everything here is register
 * numbers and byte order, which is exactly the part worth testing, and a
 * file that calls the real I2C peripheral can only ever be tested on the
 * board. The bus arrives as function pointers, so the tests hand it a fake
 * that replays canned answers and records what was written. Same shape as
 * `ota_update_port_t`, wired in composition rather than by including down.
 *
 * **Every fact below is from the datasheet**, Analog Devices ADXL345 Rev 0,
 * read on 2026-11-02 from a SparkFun mirror because analog.com could not be
 * reached from this machine. Table 16 for the register map, Table 6 for the
 * rate codes, Table 19 for the FIFO modes, the I2C section for the address.
 * A mirror is second hand, so the first `adxl345_begin` that returns OK on
 * the board is what turns this from read into proved.
 */

/// 7-bit address with SDO/ALT ADDRESS grounded. 0x1D if it is tied high.
#define ADXL345_ADDRESS_ALT_LOW   0x53U

/// What DEVID must read. Anything else is not this part, or not answering.
#define ADXL345_DEVICE_ID         0xE5U

/// How many samples the FIFO holds in stream mode.
#define ADXL345_FIFO_DEPTH        32U

/// Milli-g per count in full resolution mode, constant across every range.
#define ADXL345_MILLI_G_PER_COUNT 4

typedef enum {
    ADXL345_OK = 0,        ///< The device answered and did what was asked.
    ADXL345_BUS_ERROR,     ///< The bus refused. Wiring, address, or absent.
    ADXL345_WRONG_DEVICE   ///< Something answered, but DEVID was not 0xE5.
} adxl345_result_t;

/**
 * @brief The bus this device sits on, injected rather than included.
 *
 * Both calls return 0 on success and anything else on failure, which is
 * deliberately NOT ::i2c_status_t: this header must stay free of the
 * hardware driver so the tests can build it on a PC.
 */
typedef struct {
    void *ctx;  ///< Passed back untouched. The adapter keeps the address here.
    int (*write)(void *ctx, const uint8_t *bytes, uint8_t count);
    int (*write_then_read)(void *ctx, const uint8_t *tx, uint8_t tx_count,
                           uint8_t *rx, uint8_t rx_count);
} adxl345_bus_t;

/// One reading, in raw counts. Multiply by ::ADXL345_MILLI_G_PER_COUNT for mg.
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} adxl345_sample_t;

/**
 * @brief Check the part is there, configure it, then start it measuring.
 *
 * Reads DEVID first and goes no further if it is wrong, so a bus that
 * answers with rubbish does not end up configured. Then, with the part
 * still in standby, sets the output rate to 800 Hz, full resolution at
 * plus or minus 4 g, and the FIFO to stream mode. Measurement is turned on
 * LAST, which is the order the datasheet asks for: the part powers up in
 * standby and every setting is meant to be in place before it runs.
 */
adxl345_result_t adxl345_begin(const adxl345_bus_t *bus);

/**
 * @brief How many samples are waiting in the FIFO, 0 to 32.
 *
 * Reads FIFO_STATUS and masks it. Bit 7 of that register is FIFO_TRIG, not
 * part of the count, and a reader that takes the whole byte gets a number
 * up to 160 instead of up to 32.
 */
adxl345_result_t adxl345_fifo_level(const adxl345_bus_t *bus,
                                    uint8_t *out_entries);

/**
 * @brief Take one sample out of the FIFO.
 *
 * All six bytes come in one burst on purpose. The datasheet asks for a
 * multiple byte read so the data cannot change between two single reads,
 * which would mix one sample's X with the next sample's Z.
 */
adxl345_result_t adxl345_read_sample(const adxl345_bus_t *bus,
                                     adxl345_sample_t *out);

#endif /* ADXL345_H */
