#pragma once

#include "panics.h"
#include <stdint.h>
#include <stdbool.h>

/* Formats structured CSV messages and sends over USB CDC.
 * Messages are sent regardless of connection state — if nobody is listening
 * the bytes are dropped by the TinyUSB layer.
 * The LinuxCNC HAL component only acts on OK / HB / PANIC; all other
 * message types are logged to Postgres and ignored for machine control. */

void telemetry_panic(uint32_t uptime_s, const panic_entry_t *entry,
                     const char *state_before);
void telemetry_panic_cleared(uint32_t uptime_s, const panic_entry_t *entry);

void telemetry_heartbeat(uint32_t uptime_s, int safety_state, bool usb,
                         uint32_t loop_avg_us, uint32_t loop_max_us,
                         uint32_t overruns, uint32_t glitches);

void telemetry_transition(uint32_t uptime_s, const char *from, const char *to);

void telemetry_event(uint32_t uptime_s, const char *source, const char *event);

uint32_t telemetry_panic_count(void);
