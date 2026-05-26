/* panic_controller — ESP-IDF 6.1 */

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led.h"
#include "panics.h"
#include "relay.h"
#include "safety.h"
#include "telemetry.h"
#include "usb_serial.h"

#include <stdio.h>
#include <string.h>

static const char *state_name(safety_state_t s) {
  if (s == SAFETY_PANIC) {
    return "PANIC";
  } else if (s == SAFETY_CLEAR) {
    return "CLEAR";
  } else if (s == SAFETY_READY) {
    return "READY";
  } else if (s == SAFETY_OK) {
    return "OK";
  } else {
    return "UNKNOWN";
  }
}

static void send_status(safety_state_t state, safety_state_t *prev) {
  if (state == SAFETY_OK) {
    bool state_just_changed = (state != *prev);
    if (state_just_changed) {
      usb_serial_send("OK\n");
    }
  } else {
    usb_serial_send("PANIC\n");
  }
  *prev = state;
}

void app_main(void) {
  safety_init();
  led_init();
  usb_serial_init();

  uint32_t uptime_s = 0;
  telemetry_event(uptime_s, "system",
                  "Controller booted. Safety interlock monitoring active. "
                  "Awaiting LinuxCNC connection.");

  safety_state_t prev_safety = SAFETY_PANIC;
  bool prev_usb = false;
  safety_state_t hb_prev = SAFETY_PANIC;

  uint32_t tick = 0;
  int loop_count = 0;
  uint64_t loop_sum_us = 0;
  uint32_t loop_max_us = 0;
  uint32_t loop_overruns = 0;

  while (1) {
    int64_t t0 = esp_timer_get_time();
    uptime_s = (uint32_t)(t0 / 1000000);

    /* 1. Safety — read any incoming command before updating state so
     *    a software ESTOP takes effect on this same tick. */
    bool usb = usb_serial_connected();

    char cmd[16];
    bool got_command = usb_serial_readline(cmd, sizeof(cmd));
    if (got_command) {
      bool is_estop = (strcmp(cmd, "ESTOP") == 0);
      if (is_estop) {
        safety_force_estop();
      }
    }

    safety_state_t safety = safety_update(usb);

    /* 2. Log state transitions */
    const char *from_state   = state_name(prev_safety);
    bool        safety_changed = (safety != prev_safety);
    if (safety_changed) {
      telemetry_transition(uptime_s, from_state, state_name(safety));

      if (safety == SAFETY_PANIC) {
        telemetry_event(
            uptime_s, "safety",
            "Machine stopped. Fix the problem, then press the ack button.");
      } else if (safety == SAFETY_CLEAR) {
        telemetry_event(
            uptime_s, "safety",
            "Fault cleared. Press the ack button when you're ready to resume.");
      } else if (safety == SAFETY_READY) {
        telemetry_event(uptime_s, "safety",
                        "Ack received — confirming inputs stable for 150 ms.");
      } else if (safety == SAFETY_OK) {
        telemetry_event(uptime_s, "safety", "All clear. Machine enabled.");
      }

      prev_safety = safety;

      /* Send status immediately on any transition — don't wait for the next
       * heartbeat. hb_prev is updated so the heartbeat won't double-send. */
      send_status(safety, &hb_prev);
    }

    /* Drain faults that became active or cleared this tick. Both run every
     * tick so events while already in PANIC are never silently dropped. */
    const panic_entry_t *new_panic;
    while ((new_panic = panic_consume_new()) != NULL) {
      telemetry_panic(uptime_s, new_panic, from_state);
    }

    const panic_entry_t *cleared_panic;
    while ((cleared_panic = panic_consume_cleared()) != NULL) {
      telemetry_panic_cleared(uptime_s, cleared_panic);
    }

    bool usb_changed = (usb != prev_usb);
    if (usb_changed) {
      if (usb) {
        telemetry_event(uptime_s, "usb", "LinuxCNC connection established.");
        relay_scan_i2c(uptime_s);
      } else {
        telemetry_event(uptime_s, "usb",
                        "LinuxCNC connection lost. Machine stopped.");
      }
      prev_usb = usb;
    }

    /* 3. LED */
    led_update_state(safety, usb, tick);

    /* 4. Heartbeat — accumulate loop timing, emit every 2 s */
    loop_count++;

    int64_t work_us = esp_timer_get_time() - t0;
    uint32_t work_us_u32 = (uint32_t)work_us;
    bool overran = (work_us > LOOP_PERIOD_MS * 1000);

    loop_sum_us += work_us_u32;
    if (work_us_u32 > loop_max_us) {
      loop_max_us = work_us_u32;
    }
    if (overran) {
      loop_overruns++;
    }

    bool time_for_heartbeat = (loop_count >= HEARTBEAT_INTERVAL);
    if (time_for_heartbeat) {
      uint32_t avg_us = (uint32_t)(loop_sum_us / (uint32_t)loop_count);
      uint32_t glitches = safety_get_and_reset_glitches();

      telemetry_heartbeat(uptime_s, (int)safety, usb, avg_us,
                          loop_max_us, loop_overruns, glitches);
      send_status(safety, &hb_prev);

      char dbg[64];
      snprintf(dbg, sizeof(dbg), "GPIO raw: estop=%d ack=%d",
               gpio_get_level(SAFETY_ESTOP_GPIO),
               gpio_get_level(SAFETY_ACK_GPIO));
      telemetry_event(uptime_s, "debug", dbg);

      loop_sum_us = 0;
      loop_max_us = 0;
      loop_overruns = 0;
      loop_count = 0;
    }

    tick++;
    vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
  }
}
