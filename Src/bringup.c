/**
 * @file bringup.c
 * @brief The one shot bus diagnostic. Composition, like ota_port.c: it joins
 *        the GPIO registers, the bus driver, the device driver and the UART,
 *        and owns no rule of its own.
 *
 * The rules live in wire_probe.c, which is pure and tested on a PC. What is
 * here is the half that cannot be: writing MODER and PUPDR, reading IDR, and
 * waiting long enough for a line to settle.
 *
 * Every line it writes keeps the `id,value` shape the host parser expects, so
 * a diagnostic left on by accident cannot wedge the other side. The ids are
 * its own, so nothing it says can be mistaken for telemetry.
 */

#include "bringup.h"

#ifdef BRINGUP_DIAGNOSTIC

#include "adxl345.h"
#include "adxl_port.h"
#include "i2c.h"
#include "registers.h"
#include "uart.h"
#include "wire_probe.h"

#include <stdint.h>

/* The reserved ranges at both ends of the 7 bit space are skipped: a device
   is not allowed to live there, so probing them tells nobody anything. */
#define SCAN_FIRST_ADDRESS 0x08U
#define SCAN_LAST_ADDRESS  0x77U

/* The two addresses an ADXL345 can take, chosen by its SDO pin. */
#define ADXL345_ADDRESS_SDO_LOW  0x53U
#define ADXL345_ADDRESS_SDO_HIGH 0x1DU

#define DEVID_REGISTER  0x00U
#define SAMPLES_TO_SHOW 10U

/* Longest line here is `accz,-16000` plus the terminator. */
#define LINE_MAX 16U

#define HEX_DIGITS_PER_BYTE 2U
#define BITS_PER_HEX_DIGIT  4U
#define LOW_NIBBLE_MASK     0x0FU
#define DECIMAL_BASE        10

/* How the ports are told apart. The read adapter turns these back into base
   addresses, which keeps the pure module free of any address at all. */
#define PORT_A 0U
#define PORT_B 1U
#define PORT_C 2U

#define GPIOA_BASE 0x48000000UL
#define GPIOB_BASE 0x48000400UL
#define GPIOC_BASE 0x48000800UL
#define GPIO_MODER_AT 0x00U
#define GPIO_PUPDR_AT 0x0CU
#define GPIO_IDR_AT   0x10U

/* Pins that already have a job and must keep it. Reading the UART pair as
   plain inputs would cut the wire this report travels on, and the debug pair
   would cut the one the programmer uses. */
#define GPIOA_KEEP ((1U << 2) | (1U << 3) | (1U << 5) | (1U << 13) | (1U << 14))
#define GPIOB_KEEP (1U << 3)
#define GPIOC_KEEP (1U << 13)

/* The I2C1 pins on this board, repeated rather than shared: i2c.c keeps them
   private, and a diagnostic has no business widening another module's
   interface so it can look inside. */
#define SCL_PIN 8U
#define SDA_PIN 9U

/* Far longer than a 4.7k resistor needs to charge the few picofarads of a
   jumper wire, which is nanoseconds. */
#define SETTLE_SPINS 1000U

static char *append_text(char *at, const char *text) {
    while (*text != '\0') {
        *at++ = *text++;
    }
    return at;
}

/* The repository's fifth small decimal formatter, deliberate for the reason
   the others carry: telemetry.c's is pinned by a contract test with the
   host, and widening it so a diagnostic can print a signed milli-g would put
   this file's needs onto the module the other repository depends on. */
static char *append_int(char *at, int32_t value) {
    if (value < 0) {
        *at++ = '-';
        value = -value;
    }
    char digits[8];
    uint8_t count = 0U;
    do {
        digits[count++] = (char)('0' + (value % DECIMAL_BASE));
        value /= DECIMAL_BASE;
    } while (value != 0 && count < sizeof digits);
    while (count > 0U) {
        *at++ = digits[--count];
    }
    return at;
}

static char *append_hex_byte(char *at, uint8_t value) {
    static const char k_digits[] = "0123456789ABCDEF";
    for (uint8_t shift = HEX_DIGITS_PER_BYTE; shift > 0U; shift--) {
        const uint8_t nibble =
            (value >> ((shift - 1U) * BITS_PER_HEX_DIGIT)) & LOW_NIBBLE_MASK;
        *at++ = k_digits[nibble];
    }
    return at;
}

static void send_hex(const char *id, uint8_t value) {
    char line[LINE_MAX];
    char *at = append_text(line, id);
    *at++ = ',';
    at = append_hex_byte(at, value);
    *at++ = '\n';
    *at = '\0';
    uart_send_string(line);
}

static void send_int(const char *id, int32_t value) {
    char line[LINE_MAX];
    char *at = append_text(line, id);
    *at++ = ',';
    at = append_int(at, value);
    *at++ = '\n';
    *at = '\0';
    uart_send_string(line);
}

/* ---- the half that cannot be tested on a PC ----------------------------- */

static uint32_t base_of(uint8_t port) {
    if (port == PORT_A) {
        return GPIOA_BASE;
    }
    return (port == PORT_B) ? GPIOB_BASE : GPIOC_BASE;
}

/* The pin read wire_probe is given. Writing MODER and PUPDR then reading IDR
   is all of it. The deciding, the formatting and the sweep live in the pure
   module, where a host test drives them from a table. */
static uint32_t read_pin(void *ctx, uint8_t port, uint8_t pin,
                         wire_pull_t pull) {
    (void)ctx;
    const uint32_t base = base_of(port);
    volatile uint32_t *moder = (volatile uint32_t *)(base + GPIO_MODER_AT);
    volatile uint32_t *pupdr = (volatile uint32_t *)(base + GPIO_PUPDR_AT);
    volatile uint32_t *idr = (volatile uint32_t *)(base + GPIO_IDR_AT);
    const uint32_t chip_pull =
        (pull == WIRE_PULL_DOWN) ? GPIO_PULL_DOWN : GPIO_PULL_NONE;

    *moder = (*moder & ~GPIO_MODE_MASK(pin)) | GPIO_MODE(pin, GPIO_MODE_INPUT);
    *pupdr = (*pupdr & ~GPIO_PULL_MASK(pin)) | GPIO_PULL(pin, chip_pull);
    for (volatile uint32_t spin = 0U; spin < SETTLE_SPINS; spin++) {
        /* let the line settle */
    }
    return (*idr >> pin) & 1U;
}

static const wire_bus_t k_wires = {NULL, read_pin};

static const wire_port_t k_ports[] = {
    {'A', PORT_A, (uint16_t)GPIOA_KEEP},
    {'B', PORT_B, (uint16_t)GPIOB_KEEP},
    {'C', PORT_C, (uint16_t)GPIOC_KEEP},
};
#define PORT_COUNT (uint8_t)(sizeof k_ports / sizeof k_ports[0])

/* ---- the bus itself ------------------------------------------------------ */

/* A write of no bytes at all: the peripheral puts the address on the bus and
   then a stop. A device that is there acknowledges its address, so the
   transfer ends OK. Nothing there means NACK. No register is touched either
   way, which is what makes this safe to point at an address whose owner is
   unknown. */
static uint32_t address_answers(uint8_t address) {
    uint8_t nothing = 0U;
    return (i2c1_write(address, &nothing, 0U) == I2C_OK) ? 1U : 0U;
}

static uint8_t scan_the_bus(void) {
    uint8_t found = 0U;
    for (uint8_t address = SCAN_FIRST_ADDRESS; address <= SCAN_LAST_ADDRESS;
         address++) {
        if (address_answers(address) != 0U) {
            send_hex("scan", address);
            found++;
        }
    }
    if (found == 0U) {
        uart_send_string("scan,none\n");
    }
    return found;
}

/* Read DEVID from an address without configuring anything, so a part that is
   not an ADXL345 is never written to. */
static void report_devid(uint8_t address) {
    const uint8_t which = DEVID_REGISTER;
    uint8_t value = 0U;
    if (i2c1_write_then_read(address, &which, 1U, &value, 1U) == I2C_OK) {
        send_hex("devid", value);
    }
}

static void report_samples(void) {
    switch (adxl345_begin(adxl_bus())) {
    case ADXL345_OK:
        uart_send_string("begin,ok\n");
        break;
    case ADXL345_WRONG_DEVICE:
        uart_send_string("begin,wrong\n");
        return;
    case ADXL345_BUS_ERROR:
    default:
        uart_send_string("begin,buserr\n");
        return;
    }

    for (uint8_t taken = 0U; taken < SAMPLES_TO_SHOW; taken++) {
        adxl345_sample_t sample = {0, 0, 0};
        if (adxl345_read_sample(adxl_bus(), &sample) != ADXL345_OK) {
            uart_send_string("sample,failed\n");
            return;
        }
        /* Milli-g, so a board lying flat reads about 1000 on one axis and
           about 0 on the other two. That is the check this exists for: an
           identified part can still return nonsense. */
        send_int("accx", (int32_t)sample.x * ADXL345_MILLI_G_PER_COUNT);
        send_int("accy", (int32_t)sample.y * ADXL345_MILLI_G_PER_COUNT);
        send_int("accz", (int32_t)sample.z * ADXL345_MILLI_G_PER_COUNT);
    }
}

void bringup_report(void) {
    uart_send_string("bringup,start\n");

    /* The wires first. If a line is not there, every answer the bus gives
       afterwards is about a bus that does not exist. */
    wire_probe_report(&k_wires, "wscl", PORT_B, SCL_PIN, uart_send_string);
    wire_probe_report(&k_wires, "wsda", PORT_B, SDA_PIN, uart_send_string);
    (void)wire_probe_sweep(&k_wires, k_ports, PORT_COUNT, uart_send_string);
    i2c1_init(); /* the sweep left those pins as plain inputs */

    if (scan_the_bus() != 0U) {
        report_devid(ADXL345_ADDRESS_SDO_LOW);
        report_devid(ADXL345_ADDRESS_SDO_HIGH);
        report_samples();
    }
    uart_send_string("bringup,end\n");
}

#endif /* BRINGUP_DIAGNOSTIC */
