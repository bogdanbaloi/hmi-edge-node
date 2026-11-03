#ifndef ADXL_PORT_H
#define ADXL_PORT_H

#include "adxl345.h"
#include "vib_telemetry.h"

/**
 * @file adxl_port.h
 * @brief What the ADXL345 driver is wired to on this board.
 *
 * Composition, like ota_port.c: it joins the I2C driver to the device
 * driver and touches no register itself. It exists so adxl345.c can stay
 * free of i2c.h and keep running on a PC in the tests.
 */

/// The bus, with the device address already bound. Needs i2c1_init first.
const adxl345_bus_t *adxl_bus(void);

/// The same device seen as the two questions vib_telemetry asks: how many
/// samples are waiting, and give me the next one. Translating here is what
/// keeps vib_telemetry free of this device's header.
const vib_sensor_t *adxl_stream(void);

#endif /* ADXL_PORT_H */
