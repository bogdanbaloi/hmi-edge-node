/**
 * @file confirm_record.c
 * @brief The CONFIRMED record's layout. See confirm_record.h for the rules.
 */

#include "confirm_record.h"
#include "le_bytes.h"

/// Where each field sits in the record.
#define CONFIRM_RECORD_MARKER_AT 0U
#define CONFIRM_RECORD_IDENTITY_AT 4U

void confirm_record_build(uint8_t out[CONFIRM_RECORD_BYTES],
                          uint32_t identity) {
    le_write32(&out[CONFIRM_RECORD_MARKER_AT], CONFIRM_RECORD_MARKER);
    le_write32(&out[CONFIRM_RECORD_IDENTITY_AT], identity);
}

uint32_t confirm_record_confirms(const uint8_t raw[CONFIRM_RECORD_BYTES],
                                 uint32_t identity) {
    const uint32_t marker = le_read32(&raw[CONFIRM_RECORD_MARKER_AT]);
    const uint32_t confirmed = le_read32(&raw[CONFIRM_RECORD_IDENTITY_AT]);
    return ((marker == CONFIRM_RECORD_MARKER) && (confirmed == identity)) ? 1U
                                                                          : 0U;
}
