#pragma once

#include "safety.h"
#include <stdint.h>
#include <stdbool.h>

#define LED_GPIO 38

/* Heartbeat flash period — must match HEARTBEAT_INTERVAL in main.c */
#define HEARTBEAT_INTERVAL  200              /* 200 × 10 ms = 2 s */
#define SLOW_PULSE_TICKS    (1000 / LOOP_PERIOD_MS)  /* 1 Hz blink for no-connection */

void led_init(void);
void led_set_grb(uint8_t g, uint8_t r, uint8_t b);
void led_update_state(safety_state_t safety, bool usb_ok, uint32_t tick);

#define LED_OFF() led_set_grb(0, 0, 0)
