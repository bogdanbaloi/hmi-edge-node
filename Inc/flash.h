#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/**
 * @file flash.h
 * @brief The flash banks of the STM32L476RG: which one runs, and writing the
 *        other one. HAL: the only place SYSCFG and the flash controller are
 *        touched.
 *
 * Knows nothing about updates. The update port is built from it in the
 * composition layer (ota_port.c), the same way `adc` knows counts and never
 * degrees.
 *
 * **What it refuses to do, by construction:** touch the bank the CPU is
 * running from, write outside the spare bank's window, and change option
 * bytes. The option byte registers are not even defined in registers.h,
 * because RDP level 2 locks the chip forever and that step belongs to piece 7.
 *
 * Every sequence here is from RM0351 Rev 9, sections 3.3.5 to 3.3.8.
 */

/// One flash bank of the STM32L476RG: 512 KB (RM0351, dual bank mode).
#define FLASH_BANK_BYTES (512UL * 1024UL)
/// One flash page, the smallest erasable unit: 2 KB.
#define FLASH_PAGE_BYTES (2UL * 1024UL)
/// The bank the CPU runs from is mapped here, whichever one it is.
#define FLASH_RUNNING_BASE 0x08000000UL
/// The other bank is seen here, right after it.
#define FLASH_SPARE_BASE (FLASH_RUNNING_BASE + FLASH_BANK_BYTES)
/// Programming granularity: one double word, 64 bits (RM0351 section 3.3.7).
#define FLASH_WRITE_BYTES 8U
/// The last page of the running bank, kept for the CONFIRMED record. The
/// linker stops the image before it (piece 4), so nothing else is there.
#define FLASH_CONFIRM_BASE \
    (FLASH_RUNNING_BASE + FLASH_BANK_BYTES - FLASH_PAGE_BYTES)

/**
 * The two PHYSICAL banks, numbered as ST numbers them in RM0351.
 *
 * Physical matters: MER1 and MER2 erase physical banks, while FB_MODE decides
 * which physical bank is seen at FLASH_RUNNING_BASE. RM0351 says so in
 * section 3.5: for one address, the protection registers of bank 1 apply when
 * booting from bank 1, and those of bank 2 "if the two banks are swapped".
 * Getting this backwards would erase the running image.
 */
typedef enum {
    FLASH_BANK_1 = 1,
    FLASH_BANK_2 = 2
} flash_bank_t;

/// How an erase or a program ended.
typedef enum {
    FLASH_OK = 0,
    FLASH_REFUSED,  ///< The request itself was wrong, nothing was attempted.
    FLASH_FAILED    ///< The controller reported an error flag.
} flash_status_t;

/// Turn on what reading the bank needs. Call once, at start-up.
void flash_init(void);

/**
 * @brief The bank the CPU runs from, the one mapped at FLASH_RUNNING_BASE.
 *
 * From SYSCFG_MEMRMP.FB_MODE, which the boot code sets from the BFB2 option
 * bit, so it is the bank really running, not the one that was meant to.
 */
flash_bank_t flash_running_bank(void);

/// The other bank, the one an update is written into. Never the running one.
flash_bank_t flash_spare_bank(void);

/// The spare bank as bytes, for reading back what was written (the CRC at
/// COMMIT). Read only: writing through this pointer does nothing, flash is
/// written through the controller.
const uint8_t *flash_spare_image(void);

/// The same memory seen as words. A bank starts on a bank boundary, so it is
/// word aligned, and the alignment lives HERE, in the one place that knows the
/// address, instead of being re-derived from a byte pointer by a cast.
const uint32_t *flash_spare_words(void);

/**
 * @brief Erase the whole spare bank, one mass erase (RM0351 section 3.3.6).
 *
 * Takes about 22 ms typical and 24.59 ms at most (DS10198 Rev 8, Table 63),
 * and the CPU keeps running from the other bank throughout (read-while-write,
 * section 3.3.8). The bank's last page goes too, which is correct: it holds
 * that image's CONFIRMED record, and the image is being replaced.
 *
 * @return ::FLASH_OK, or ::FLASH_FAILED with the controller's error flags set.
 */
flash_status_t flash_erase_spare(void);

/**
 * @brief Program bytes into the spare bank.
 *
 * @param offset Where in the spare bank, a multiple of ::FLASH_WRITE_BYTES.
 * @param bytes  What to write.
 * @param len    How many, a multiple of ::FLASH_WRITE_BYTES.
 * @return ::FLASH_REFUSED if the offset or the length is not aligned, or the
 *         write would run past the bank; ::FLASH_FAILED if the controller
 *         reported an error; ::FLASH_OK when every double word went in.
 */
flash_status_t flash_program_spare(uint32_t offset, const uint8_t *bytes,
                                   uint32_t len);

/// The CONFIRMED record of the RUNNING image, for reading only. Its layout is
/// confirm_record.h; whether it confirms anything is that module's question.
const uint8_t *flash_confirm_record(void);

/**
 * @brief Write the CONFIRMED record of the running image.
 *
 * The one place this firmware writes the bank it is running from. The CPU
 * stalls while the double word goes in, about 90 us at most (RM0351 section
 * 3.3.5), which is why it happens while answering CONFIRM and never while
 * image bytes are arriving.
 *
 * Writing over an already written record would set PROGERR, so a record that
 * is already there is left alone: CONFIRM repeated is CONFIRM once, which is
 * the idempotency the protocol asks for.
 *
 * @return ::FLASH_OK, or ::FLASH_FAILED with the controller's error flags.
 */
flash_status_t flash_write_confirm_record(
    const uint8_t record[FLASH_WRITE_BYTES]);

#endif /* FLASH_H */
