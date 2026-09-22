/**
 * @file flash.c
 * @brief The flash banks. The only place SYSCFG and the flash controller are
 *        touched. Sequences from RM0351 Rev 9, sections 3.3.5 to 3.3.8.
 */

#include "flash.h"
#include "registers.h"

/// A double word is written as two 32-bit words, low one first.
#define FLASH_WORD_BYTES (FLASH_WRITE_BYTES / 2U)
/// Bits to shift per byte when packing them into a word.
#define FLASH_BITS_PER_BYTE 8U

void flash_init(void) {
    /* SYSCFG is clock-gated at reset, and a gated peripheral reads as 0,
       which would report bank 1 from either bank. */
    RCC_APB2ENR |= RCC_APB2ENR_SYSCFGEN;
}

flash_bank_t flash_running_bank(void) {
    return ((SYSCFG_MEMRMP & SYSCFG_MEMRMP_FB_MODE) != 0U) ? FLASH_BANK_2
                                                          : FLASH_BANK_1;
}

flash_bank_t flash_spare_bank(void) {
    return (flash_running_bank() == FLASH_BANK_1) ? FLASH_BANK_2
                                                  : FLASH_BANK_1;
}

const uint8_t *flash_spare_image(void) {
    return (const uint8_t *)FLASH_SPARE_BASE;
}

const uint32_t *flash_spare_words(void) {
    return (const uint32_t *)FLASH_SPARE_BASE;
}

const uint32_t *flash_running_words(void) {
    return (const uint32_t *)FLASH_RUNNING_BASE;
}

/// Placed by the linker script at the end of everything loaded into flash.
/// Declared as an array so the symbol itself is the address, never read.
extern const uint8_t k_firmware_image_end[];

uint32_t flash_image_bytes(void) {
    return (uint32_t)(k_firmware_image_end -
                      (const uint8_t *)FLASH_RUNNING_BASE);
}

const uint8_t *flash_confirm_record(void) {
    return (const uint8_t *)FLASH_CONFIRM_BASE;
}

static void wait_while_busy(void) {
    while ((FLASH_SR & FLASH_SR_BSY) != 0U) {
    }
}

/// Section 3.3.5: the two keys, in this order. A wrong sequence locks the
/// register until the next reset, so it is written once and never guessed.
static void unlock(void) {
    if ((FLASH_CR & FLASH_CR_LOCK) != 0U) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

static void lock(void) {
    FLASH_CR |= FLASH_CR_LOCK;
}

/// Errors must be clear before an operation, or the next one reports PGSERR
/// for a fault that was already there. Writing a flag back clears it.
static void clear_errors(void) {
    FLASH_SR = FLASH_SR_ERRORS | FLASH_SR_EOP;
}

/// Section 3.3.7, "Programming and caches": after writing or erasing flash,
/// the data cache may still hold what used to be there. Everything that reads
/// the spare bank back, the CRC above all, would read the old bytes.
static void flush_data_cache(void) {
    FLASH_ACR &= ~FLASH_ACR_DCEN;  /* only resettable while disabled */
    FLASH_ACR |= FLASH_ACR_DCRST;
    FLASH_ACR &= ~FLASH_ACR_DCRST;
    FLASH_ACR |= FLASH_ACR_DCEN;
}

/// FLASH_OK when the controller reported no error.
static flash_status_t status_after_operation(void) {
    return ((FLASH_SR & FLASH_SR_ERRORS) != 0U) ? FLASH_FAILED : FLASH_OK;
}

flash_status_t flash_erase_spare(void) {
    /* MER1 and MER2 erase PHYSICAL banks, and FB_MODE decides which physical
       bank the CPU runs from, so this bit is chosen from the running bank and
       never from an address. The wrong one here erases the running image. */
    const uint32_t mass_erase_bit =
        (flash_spare_bank() == FLASH_BANK_1) ? FLASH_CR_MER1 : FLASH_CR_MER2;

    wait_while_busy();
    unlock();
    clear_errors();

    FLASH_CR |= mass_erase_bit;
    FLASH_CR |= FLASH_CR_START;
    wait_while_busy();

    const flash_status_t status = status_after_operation();
    FLASH_CR &= ~mass_erase_bit;
    lock();
    flush_data_cache();
    return status;
}

/// One double word, the only size the controller accepts (section 3.3.7).
static flash_status_t program_double_word(uint32_t address,
                                          const uint8_t *bytes) {
    uint32_t low = 0U;
    uint32_t high = 0U;
    for (uint32_t i = 0U; i < FLASH_WORD_BYTES; i++) {
        low |= (uint32_t)bytes[i] << (FLASH_BITS_PER_BYTE * i);
        high |= (uint32_t)bytes[i + FLASH_WORD_BYTES]
                << (FLASH_BITS_PER_BYTE * i);
    }

    clear_errors();
    FLASH_CR |= FLASH_CR_PG;
    REG32(address) = low;
    REG32(address + FLASH_WORD_BYTES) = high;
    wait_while_busy();

    const flash_status_t status = status_after_operation();
    FLASH_CR &= ~FLASH_CR_PG;
    return status;
}

/// Erased flash reads as all ones, so this is what "nothing written yet"
/// looks like in one byte.
#define FLASH_ERASED_BYTE 0xFFU

/// What the record page holds: exactly what we would write, something else, or
/// nothing at all.
typedef enum {
    RECORD_ERASED = 0,
    RECORD_SAME,
    RECORD_DIFFERENT
} record_state_t;

static record_state_t record_state(const uint8_t *wanted) {
    const uint8_t *existing = flash_confirm_record();
    uint32_t erased = 1U;
    uint32_t same = 1U;
    for (uint32_t i = 0U; i < FLASH_WRITE_BYTES; i++) {
        if (existing[i] != FLASH_ERASED_BYTE) {
            erased = 0U;
        }
        if (existing[i] != wanted[i]) {
            same = 0U;
        }
    }
    if (erased != 0U) {
        return RECORD_ERASED;
    }
    return (same != 0U) ? RECORD_SAME : RECORD_DIFFERENT;
}

flash_status_t flash_write_confirm_record(
    const uint8_t record[FLASH_WRITE_BYTES]) {
    const record_state_t state = record_state(record);
    if (state == RECORD_SAME) {
        return FLASH_OK;  /* already written, and writing twice fails */
    }
    if (state == RECORD_DIFFERENT) {
        /* Something else is in the page: a record of another image, or half a
           record left by a power cut. Either way this write cannot happen
           without erasing the page, so say so instead of answering OK to a
           confirmation that will not hold. */
        return FLASH_FAILED;
    }

    wait_while_busy();
    unlock();
    const flash_status_t status = program_double_word(FLASH_CONFIRM_BASE,
                                                      record);
    lock();
    flush_data_cache();
    return status;
}

flash_status_t flash_program_spare(uint32_t offset, const uint8_t *bytes,
                                   uint32_t len) {
    /* Refused, not attempted: an unaligned or overlong write would set
       PGAERR or SIZERR anyway, but the guard also keeps every address this
       function touches inside the spare bank's IMAGE area. Its last page is
       that bank's CONFIRMED record: image bytes landing there could form a
       record that looks valid, and the installed image would boot already
       confirmed, without the trial the record exists to require. */
    if ((offset % FLASH_WRITE_BYTES) != 0U ||
        (len % FLASH_WRITE_BYTES) != 0U ||
        len > FLASH_IMAGE_BYTES ||
        offset > FLASH_IMAGE_BYTES - len) {
        return FLASH_REFUSED;
    }

    wait_while_busy();
    unlock();

    flash_status_t status = FLASH_OK;
    for (uint32_t done = 0U; done < len && status == FLASH_OK;
         done += FLASH_WRITE_BYTES) {
        status = program_double_word(FLASH_SPARE_BASE + offset + done,
                                     &bytes[done]);
    }

    lock();
    flush_data_cache();
    return status;
}
