#pragma once

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * GPIO assignments — Waveshare ESP32-S3-POE-ETH-8DI-8RO
 *
 * Safety inputs are opto-isolated with open-collector output and internal pull-up.
 * Opto on (circuit active) pulls GPIO LOW. Opto off → pull-up holds GPIO HIGH.
 * danger_level = true: HIGH = danger (fail-safe: wire break → opto off → HIGH → fault).
 *
 * SAFETY_ZONE_GPIO  (4)  DI1: exclusion zone sensor — LOW = clear, HIGH = person/wire fault.
 * SAFETY_ESTOP_GPIO (5)  DI2: HMI panel e-stop (NC) — LOW = released, HIGH = pressed or wire fault.
 * SAFETY_ACK_GPIO   (6)  DI3: operator ack (NO)     — LOW = pressed (opto on), HIGH = released.
 *
 * Safety outputs are relay contacts driven via TCA9554 I2C expander (relay.h).
 * -------------------------------------------------------------------------- */
#define SAFETY_ZONE_GPIO        4
#define SAFETY_ESTOP_GPIO       5
#define SAFETY_ACK_GPIO         6
#define SAFETY_24V_GPIO         18  /* DI8 — 24V PSU presence (HIGH = absent = fault) */
#define SAFETY_ZERO_SPEED_GPIO  7   /* DI4 — motor driver zero speed (NPN, LOW = stopped) */

/* Timing constants shared across safety, LED, and heartbeat logic */
#define LOOP_PERIOD_MS       10   /* main loop tick period                          */
#define GLITCH_THRESHOLD_MS  50   /* input pulses shorter than this are discarded   */
#define CALM_SWEEP_MS        300  /* purple→cyan sweep duration on PANIC→CLEAR      */
#define CLEAR_SETTLE_MS      150  /* cyan→lime settle duration after ack before OK  */

typedef enum {
    SAFETY_PANIC = 0,  /* fault active — zero is the safe default */
    SAFETY_CLEAR = 1,  /* fault cleared, waiting for operator ack */
    SAFETY_READY = 2,  /* ack received, inputs settling before OK */
    SAFETY_OK    = 3,  /* stable and clear — machine may run      */
} safety_state_t;

/* Watchdog escalation level — advances when motor doesn't stop after a panic */
typedef enum {
    WD_IDLE       = 0,  /* no panic active, or motor confirmed stopped before T1  */
    WD_MONITORING = 1,  /* panic active, counting T1, waiting for zero speed       */
    WD_HW_ESTOP   = 2,  /* T1 expired — HW estop triggered, counting T2           */
    WD_CONTACTOR  = 3,  /* T2 expired — contactor tripped, latched until OK       */
} wd_state_t;

void            safety_init(void);
safety_state_t  safety_update(bool usb_connected);
void            safety_force_estop(void);
wd_state_t      safety_wd_state(void);
uint32_t        safety_calm_progress(void);    /* 0..CALM_SWEEP_LOOPS, for LED sweep    */
uint32_t        safety_settle_progress(void);  /* 0..CLEAR_SETTLE_LOOPS, for LED sweep  */
uint32_t        safety_get_and_reset_glitches(void);
