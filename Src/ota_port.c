/**
 * @file ota_port.c
 * @brief The update port on the Nucleo. See ota_port.h for what is real yet.
 */

#include "ota_port.h"
#include "confirm_record.h"
#include "option_bytes.h"
#include "core.h"
#include "crc32.h"
#include "crc_unit.h"
#include "flash.h"
#include "uart.h"

/// The image may fill its bank except the last page, which holds the
/// CONFIRMED record (answer 3 to industrial-hmi): 522240 bytes, as pinned in
/// the spec. The driver owns the number; this is the same one.
#define OTA_PORT_IMAGE_CAPACITY FLASH_IMAGE_BYTES

/**
 * What identifies the image that is running now: the checksum of everything
 * the linker loaded into flash. Two builds differ somewhere in there, so a
 * CONFIRMED record written for one cannot confirm another, which a version
 * constant could not promise (see confirm_record.h).
 */
static uint32_t running_identity(void) {
    const uint32_t bytes = flash_image_bytes();
    return (crc_unit_is_trustworthy() != 0U)
               ? crc_unit_compute_words(flash_running_words(), bytes)
               : crc32_compute((const uint8_t *)flash_running_words(), bytes);
}

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
                                                running_identity()) != 0U)
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

/**
 * Writes the option bytes so the next boot comes from the bank the image was
 * just written into. It does NOT reset: the host is waiting for the ACK to
 * COMMIT, and request_reset() below applies the options once that ACK is out.
 *
 * A build that is not armed refuses here, which is honest and is what the
 * board has been answering since piece 5: the image is written and verified,
 * and the switch is the step it will not take.
 */
static ota_io_t select_new_bank(void *ctx) {
    (void)ctx;
    return (option_bytes_boot_from(flash_spare_bank()) == OPTION_WRITE_OK)
               ? OTA_IO_OK
               : OTA_IO_FAILED;
}

/// Keeps the running image: writes the record that says so. Repeating it is
/// harmless, because a record already in place is left alone.
static ota_io_t confirm(void *ctx) {
    (void)ctx;
    uint8_t record[FLASH_WRITE_BYTES];
    confirm_record_build(record, running_identity());
    return (flash_write_confirm_record(record) == FLASH_OK) ? OTA_IO_OK
                                                            : OTA_IO_FAILED;
}

/// Applies the option bytes written by select_new_bank, which resets the
/// board. Called after the ACK to COMMIT has gone out, so the host sees the
/// answer and then the silence of a reboot, in that order.
static void request_reset(void *ctx) {
    (void)ctx;
    option_bytes_launch();
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
