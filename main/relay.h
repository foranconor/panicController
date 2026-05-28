#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Relay outputs via TCA9554 I2C expander (0x20), SDA=GPIO42, SCL=GPIO41.
 * Relay de-energised by default — fail-safe on power-up or I2C fault. */

typedef enum {
    RELAY_CONTACTOR = 0,  /* RO1 — power contactor upstream of motor drivers */
    RELAY_HW_ESTOP  = 1,  /* RO2 — hardware estop input on motor driver       */
} relay_id_t;

void relay_init(void);
void relay_set(relay_id_t id, bool on);
void relay_scan_i2c(uint32_t uptime_s);  /* emit all I2C device addresses via telemetry */
