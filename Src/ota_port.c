/**
 * @file ota_port.c
 * @brief The update port on the Nucleo. See ota_port.h for what is real yet.
 */

#include "ota_port.h"
#include "confirm_record.h"
#include "core.h"
#include "crc32.h"
#include "crc_unit.h"
#include "flash.h"
#include "uart.h"

/// The image may fill its bank except the last page, which holds the
/// CONFIRMED record (answer 3 to industrial-hmi): 522240 bytes, as pinned in
/// the spec.
#define OTA_PORT_IMAGE_CAPACITY (FLASH_BANK_BYTES - FLASH_PAGE_BYTES)

static uint32_t now_ms(void *ctx) {
    (void)ctx;
    return core_millis();
}

static void send(void *ctx, const uint8_t *bytes, size_t len) {
    (void)ctx;
    uart_send_bytes(bytes, len);
}

/**
 * The driver names the bank the way ST does; the protocol's numbers are
 * mapped here, in the one place that knows both.
 *
 * The state comes from the record in flash, not from a variable in RAM: a
 * variable would say CONFIRMED again after every reset, which is exactly the
 * mistake the record exists to prevent.
 */
static void running(void *ctx, ota_running_t *out) {
    (void)ctx;
    out->version = OTA_PORT_FIRMWARE_VERSION;
    out->active_bank = (flash_running_bank() == FLASH_BANK_2)
                           ? (uint8_t)OTA_BANK_2
                           : (uint8_t)OTA_BANK_1;
    out->image_state = (confirm_record_confirms(flash_confirm_record(),
                                                OTA_PORT_FIRMWARE_VERSION) != 0U)
                           ? (uint8_t)OTA_IMAGE_CONFIRMED
                           : (uint8_t)OTA_IMAGE_TRIAL;
}

/// One mass erase of the spare bank, about 24.59 ms at worst.
static ota_io_t erase_inactive(void *ctx) {
    (void)ctx;
    return (flash_erase_spare() == FLASH_OK) ? OTA_IO_OK : OTA_IO_FAILED;
}

static ota_io_t program(void *ctx, uint32_t offset, const uint8_t *bytes,
                        uint16_t len) {
    (void)ctx;
    return (flash_program_spare(offset, bytes, len) == FLASH_OK) ? OTA_IO_OK
                                                                 : OTA_IO_FAILED;
}

/**
 * Read back what is in the spare bank, rather than adding up what was sent:
 * this is what catches a byte that never made it into flash.
 *
 * The peripheral does it in a fraction of the time, measured: software took
 * about 4.6 s for a full image and the host allows 2 s. The software version
 * stays as the fallback for a unit that failed its own check at start-up,
 * because a wrong CRC would fail every COMMIT on a good image.
 */
static uint32_t image_crc32(void *ctx, uint32_t size) {
    (void)ctx;
    return (crc_unit_is_trustworthy() != 0U)
               ? crc_unit_compute_words(flash_spare_words(), size)
               : crc32_compute(flash_spare_image(), size);
}

/// Piece 7 switches banks through BFB2. Until then, never.
static ota_io_t select_new_bank(void *ctx) {
    (void)ctx;
    return OTA_IO_FAILED;
}

/// Keeps the running image: writes the record that says so. Repeating it is
/// harmless, because a record already in place is left alone.
static ota_io_t confirm(void *ctx) {
    (void)ctx;
    uint8_t record[FLASH_WRITE_BYTES];
    confirm_record_build(record, OTA_PORT_FIRMWARE_VERSION);
    return (flash_write_confirm_record(record) == FLASH_OK) ? OTA_IO_OK
                                                            : OTA_IO_FAILED;
}

/// Piece 7. Unreachable before it: a reset follows only an accepted COMMIT.
static void request_reset(void *ctx) {
    (void)ctx;
}

static const ota_update_port_t k_port = {
    .ctx = NULL,
    .image_capacity = OTA_PORT_IMAGE_CAPACITY,
    .now_ms = now_ms,
    .send = send,
    .running = running,
    .erase_inactive = erase_inactive,
    .program = program,
    .image_crc32 = image_crc32,
    .select_new_bank = select_new_bank,
    .confirm = confirm,
    .request_reset = request_reset,
};

const ota_update_port_t *ota_port(void) {
    return &k_port;
}
