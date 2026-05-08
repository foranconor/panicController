#pragma once

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * GPIO assignments
 *
 * Safety inputs: pull-down, LOW = danger (fail-safe).
 * Wire break → 0 V → FAULT. Hold HIGH (3.3 V) to assert safe.
 *
 * SAFETY_ZONE_GPIO  (4): exclusion zone presence sensor — HIGH = clear, LOW = person detected.
 * SAFETY_ESTOP_GPIO (5): HMI panel e-stop             — HIGH = released, LOW = pressed or wire fault.
 * SAFETY_ACK_GPIO   (0): operator acknowledge button  — active low, pull-up.
 *                        Currently the ESP32 BOOT button (PoC proxy).
 * -------------------------------------------------------------------------- */
#define SAFETY_ZONE_GPIO    4
#define SAFETY_ESTOP_GPIO   5
#define SAFETY_ACK_GPIO     0
#define SAFETY_OUTPUT_GPIO  9   /* HIGH = safe (OK), LOW = fault/not ready */

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

void            safety_init(void);
safety_state_t  safety_update(bool usb_connected);
void            safety_force_estop(void);
uint32_t        safety_calm_progress(void);    /* 0..CALM_SWEEP_LOOPS, for LED sweep    */
uint32_t        safety_settle_progress(void);  /* 0..CLEAR_SETTLE_LOOPS, for LED sweep  */
uint32_t        safety_get_and_reset_glitches(void);
