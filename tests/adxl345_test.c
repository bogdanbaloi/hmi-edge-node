/**
 * @file adxl345_test.c
 * @brief The accelerometer driver against a fake bus.
 *
 * The point of injecting the bus is this file. Every register number, every
 * byte order and every refusal is checked here on a PC, so the only thing
 * left for the board is whether the wires are right.
 *
 * Build and run: `mingw32-make -C tests run` (see tests/Makefile).
 */

#include "adxl345.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned g_checks;
static unsigned g_failures;

static void check(int condition, const char *what, int line) {
    g_checks++;
    if (!condition) {
        g_failures++;
        (void)printf("  FAIL line %d: %s\n", line, what);
    }
}

/* ---- The fake bus -------------------------------------------------------
 * Records every write so the test can assert what was configured and in
 * what order, and replays a canned answer for each read. `fail_after` turns
 * the bus hostile from a chosen call onwards, which is how the refusal
 * paths get exercised without any hardware.
 */
#define FAKE_MAX_WRITES   8
#define FAKE_WRITE_BYTES  4
#define FAKE_MAX_READ     8
#define FAKE_NEVER_FAIL   255

typedef struct {
    uint8_t written[FAKE_MAX_WRITES][FAKE_WRITE_BYTES];
    uint8_t written_len[FAKE_MAX_WRITES];
    uint8_t write_count;

    uint8_t last_register;      /* the register the last read asked for */
    uint8_t reply[FAKE_MAX_READ];
    uint8_t read_count;

    uint8_t calls;              /* reads plus writes, for fail_after */
    uint8_t fail_after;
} fake_bus_t;

static int fake_should_fail(fake_bus_t *fake) {
    fake->calls++;
    return (fake->calls > fake->fail_after) ? 1 : 0;
}

static int fake_write(void *ctx, const uint8_t *bytes, uint8_t count) {
    fake_bus_t *fake = (fake_bus_t *)ctx;
    if (fake_should_fail(fake)) {
        return -1;
    }
    if (fake->write_count < FAKE_MAX_WRITES) {
        const uint8_t room = (count < FAKE_WRITE_BYTES) ? count
                                                        : FAKE_WRITE_BYTES;
        (void)memcpy(fake->written[fake->write_count], bytes, room);
        fake->written_len[fake->write_count] = room;
        fake->write_count++;
    }
    return 0;
}

static int fake_write_then_read(void *ctx, const uint8_t *tx, uint8_t tx_count,
                                uint8_t *rx, uint8_t rx_count) {
    fake_bus_t *fake = (fake_bus_t *)ctx;
    if (fake_should_fail(fake)) {
        return -1;
    }
    if (tx_count > 0U) {
        fake->last_register = tx[0];
    }
    for (uint8_t i = 0U; i < rx_count; i++) {
        rx[i] = (i < FAKE_MAX_READ) ? fake->reply[i] : 0U;
    }
    fake->read_count = rx_count;
    return 0;
}

static void fake_reset(fake_bus_t *fake, adxl345_bus_t *bus) {
    (void)memset(fake, 0, sizeof(*fake));
    fake->fail_after = FAKE_NEVER_FAIL;
    bus->ctx = fake;
    bus->write = fake_write;
    bus->write_then_read = fake_write_then_read;
}

/// Find what was written to `reg`, or -1 when that register was never set.
static int value_written_to(const fake_bus_t *fake, uint8_t reg) {
    for (uint8_t i = 0U; i < fake->write_count; i++) {
        if ((fake->written_len[i] >= 2U) && (fake->written[i][0] == reg)) {
            return (int)fake->written[i][1];
        }
    }
    return -1;
}

/* ---- The register numbers the driver is expected to use -----------------
 * Written out here rather than shared with the driver on purpose. A test
 * that imports the same constant as the code proves only that the constant
 * equals itself. These come from Table 16 of the datasheet.
 */
#define EXPECT_REG_BW_RATE      0x2CU
#define EXPECT_REG_POWER_CTL    0x2DU
#define EXPECT_REG_DATA_FORMAT  0x31U
#define EXPECT_REG_DATAX0       0x32U
#define EXPECT_REG_FIFO_CTL     0x38U
#define EXPECT_REG_FIFO_STATUS  0x39U

#define EXPECT_RATE_800_HZ      0x0DU   /* Table 6, 0b1101                  */
#define EXPECT_FULL_RES_4G      0x09U   /* FULL_RES set, range 0b01         */
#define EXPECT_FIFO_STREAM      0x80U   /* mode 0b10 in bits 7:6, Table 19  */
#define EXPECT_MEASURE          0x08U   /* POWER_CTL measure bit            */

#define WRITES_IN_BEGIN         4U

static void a_wrong_device_id_stops_everything(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    fake_reset(&fake, &bus);
    fake.reply[0] = 0x00U;  /* a silent bus reads as zero on many boards */

    const adxl345_result_t result = adxl345_begin(&bus);

    check(result == ADXL345_WRONG_DEVICE, "a wrong id is named, not hidden",
          __LINE__);
    check(fake.write_count == 0U,
          "nothing is configured when the part is not ours", __LINE__);
}

static void the_right_device_is_configured_in_full(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    fake_reset(&fake, &bus);
    fake.reply[0] = ADXL345_DEVICE_ID;

    const adxl345_result_t result = adxl345_begin(&bus);

    check(result == ADXL345_OK, "a real ADXL345 starts", __LINE__);
    check(fake.write_count == WRITES_IN_BEGIN,
          "four registers are written, no more", __LINE__);
    check(value_written_to(&fake, EXPECT_REG_BW_RATE) == (int)EXPECT_RATE_800_HZ,
          "the output rate is 800 Hz", __LINE__);
    check(value_written_to(&fake, EXPECT_REG_DATA_FORMAT)
              == (int)EXPECT_FULL_RES_4G,
          "full resolution at plus or minus 4 g", __LINE__);
    check(value_written_to(&fake, EXPECT_REG_FIFO_CTL)
              == (int)EXPECT_FIFO_STREAM,
          "the FIFO is in stream mode", __LINE__);
    check(value_written_to(&fake, EXPECT_REG_POWER_CTL) == (int)EXPECT_MEASURE,
          "measurement is turned on", __LINE__);
}

/**
 * The datasheet asks for the part to be configured in standby and started
 * afterwards. A driver that starts it first would take its first samples
 * with the reset settings, which is 100 Hz with the FIFO bypassed.
 */
static void measurement_is_the_last_thing_turned_on(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    fake_reset(&fake, &bus);
    fake.reply[0] = ADXL345_DEVICE_ID;

    (void)adxl345_begin(&bus);

    check(fake.write_count == WRITES_IN_BEGIN, "four writes happened",
          __LINE__);
    check(fake.written[fake.write_count - 1U][0] == EXPECT_REG_POWER_CTL,
          "POWER_CTL is written last of all", __LINE__);
}

static void a_bus_that_fails_midway_stops_there(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    fake_reset(&fake, &bus);
    fake.reply[0] = ADXL345_DEVICE_ID;
    fake.fail_after = 2U;  /* the id read plus one write get through */

    const adxl345_result_t result = adxl345_begin(&bus);

    check(result == ADXL345_BUS_ERROR, "a failing bus is reported", __LINE__);
    check(fake.write_count == 1U, "nothing is written after the failure",
          __LINE__);
}

/**
 * Bit 7 of FIFO_STATUS is FIFO_TRIG, not part of the count. A reader that
 * takes the whole byte reports up to 160 entries from a FIFO that holds 32,
 * and the loop above it then tries to drain samples that are not there.
 */
static void the_fifo_level_ignores_the_trigger_bit(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    uint8_t entries = 0U;

    fake_reset(&fake, &bus);
    fake.reply[0] = 0x80U | 17U;  /* trigger set, seventeen waiting */

    const adxl345_result_t result = adxl345_fifo_level(&bus, &entries);

    check(result == ADXL345_OK, "the level is read", __LINE__);
    check(entries == 17U, "the trigger bit is masked off", __LINE__);
    check(fake.last_register == EXPECT_REG_FIFO_STATUS,
          "the level comes from FIFO_STATUS", __LINE__);
}

static void a_full_fifo_reads_as_its_depth(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    uint8_t entries = 0U;

    fake_reset(&fake, &bus);
    fake.reply[0] = ADXL345_FIFO_DEPTH;

    (void)adxl345_fifo_level(&bus, &entries);

    check(entries == ADXL345_FIFO_DEPTH, "a full FIFO reads as 32", __LINE__);
}

/**
 * Six bytes, low byte first, two's complement. Getting the order backwards
 * turns a reading of 1 g into something enormous, and the vibration figure
 * downstream would look like a machine tearing itself apart.
 */
static void a_sample_is_little_endian_and_signed(void) {
    fake_bus_t fake;
    adxl345_bus_t bus;
    adxl345_sample_t sample = { 0, 0, 0 };

    fake_reset(&fake, &bus);
    /* X = 0x0100 = 256, Y = 0xFFFF = -1, Z = 0xFF00 = -256 */
    fake.reply[0] = 0x00U;
    fake.reply[1] = 0x01U;
    fake.reply[2] = 0xFFU;
    fake.reply[3] = 0xFFU;
    fake.reply[4] = 0x00U;
    fake.reply[5] = 0xFFU;

    const adxl345_result_t result = adxl345_read_sample(&bus, &sample);

    check(result == ADXL345_OK, "a sample is read", __LINE__);
    check(sample.x == 256, "X takes the low byte first", __LINE__);
    check(sample.y == -1, "all ones is minus one, not 65535", __LINE__);
    check(sample.z == -256, "a negative Z survives the conversion", __LINE__);
    check(fake.last_register == EXPECT_REG_DATAX0,
          "the burst starts at DATAX0", __LINE__);
    check(fake.read_count == 6U,
          "all six bytes come in ONE transfer, so a sample cannot be split",
          __LINE__);
}

static void nothing_is_touched_without_a_bus(void) {
    adxl345_sample_t sample = { 0, 0, 0 };
    uint8_t entries = 0U;
    adxl345_bus_t empty = { NULL, NULL, NULL };

    check(adxl345_begin(NULL) == ADXL345_BUS_ERROR,
          "a missing bus is refused, not followed", __LINE__);
    check(adxl345_begin(&empty) == ADXL345_BUS_ERROR,
          "a bus with no functions is refused", __LINE__);
    check(adxl345_read_sample(&empty, &sample) == ADXL345_BUS_ERROR,
          "no sample without a bus", __LINE__);
    check(adxl345_fifo_level(&empty, &entries) == ADXL345_BUS_ERROR,
          "no level without a bus", __LINE__);
}

int main(void) {
    (void)printf("unit: the ADXL345 driver against a fake bus\n");

    a_wrong_device_id_stops_everything();
    the_right_device_is_configured_in_full();
    measurement_is_the_last_thing_turned_on();
    a_bus_that_fails_midway_stops_there();
    the_fifo_level_ignores_the_trigger_bit();
    a_full_fifo_reads_as_its_depth();
    a_sample_is_little_endian_and_signed();
    nothing_is_touched_without_a_bus();

    if (g_failures == 0U) {
        (void)printf("OK: %u checks passed\n", g_checks);
        return 0;
    }
    (void)printf("FAILED: %u of %u checks\n", g_failures, g_checks);
    return 1;
}
