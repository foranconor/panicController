#include "panics.h"

/* --------------------------------------------------------------------------
 * Fault registry - every fault this controller can report lives here.
 *
 * Each row is a complete description of one fault condition:
 *   - what it is        (title, source)
 *   - why it matters    (explanation)
 *   - how to fix it     (fix - numbered steps for the operator)
 *   - what colour to show on the LED when this fault is displaying
 *
 * The .active field is the live runtime state. Everything else is static.
 * -------------------------------------------------------------------------- */

static panic_entry_t s_panics[PANIC_COUNT] = {

    [PANIC_ESTOP_HMI] =
        {
            .title = "E-stop pressed at panel",
            .source_type = "GPIO",
            .source_id = "GPIO5",
            .explanation =
                "The panel e-stop on GPIO5 was activated. This could be a "
                "deliberate emergency stop, a button left pressed, or a wiring "
                "fault.",
            .fix = "1. Find out why it was pressed before resetting - someone "
                   "hit it for a reason.\n"
                   "2. Release the e-stop button (twist-and-turn or key-reset "
                   "depending on your button).\n"
                   "3. If the button is already released, check the wiring on "
                   "GPIO5 for a break or loose terminal.\n"
                   "4. Once the button is out and the circuit is healthy, "
                   "press the ack button to resume.",
            .color = {255, 165, 0},
            .active = false,
        },

    [PANIC_ZONE_SENSOR] =
        {
            .title = "Zone sensor triggered - person detected",
            .source_type = "GPIO",
            .source_id = "GPIO4",
            .explanation =
                "The exclusion zone sensor on GPIO4 is active. Something is in "
                "the machine's work area that shouldn't be. The machine is "
                "stopped.",
            .fix = "1. Make sure the exclusion zone is completely clear.\n"
                   "2. If the zone is clear but the sensor is still active, "
                   "check its indicator light - it may be misaligned or failing.\n"
                   "3. Inspect the wiring on GPIO4 for damage or a loose "
                   "connector.\n"
                   "4. Once the zone is clear and the sensor reads normal, "
                   "press the ack button to resume.",
            .color = {255, 255, 0},
            .active = false,
        },

    [PANIC_USB_LOST] =
        {
            .title = "Lost connection to LinuxCNC",
            .source_type = "USB",
            .source_id = "CDC0",
            .explanation = "The USB link to LinuxCNC dropped. Without it the "
                           "machine has no control path and cannot run safely.",
            .fix = "1. Check the USB cable between the controller and the "
                   "LinuxCNC machine - reseat both ends.\n"
                   "2. Confirm LinuxCNC is running and hasn't crashed.\n"
                   "3. Check that the HAL component loaded cleanly (look for "
                   "errors in the LinuxCNC terminal).\n"
                   "4. Once the link is back and stable, press the ack button "
                   "to resume.",
            .color = {255, 0, 255}, /* magenta */
            .active = false,
        },

    [PANIC_SW_ESTOP] =
        {
            .title = "Software estop from LinuxCNC",
            .source_type = "SOFTWARE",
            .source_id = "LinuxCNC",
            .explanation =
                "LinuxCNC asserted a software estop. The operator pressed the "
                "estop button in AXIS, or LinuxCNC encountered an internal "
                "fault.",
            .fix = "1. Check the LinuxCNC status bar and log for the reason.\n"
                   "2. Clear any faults in LinuxCNC.\n"
                   "3. Once everything looks good, press the ack button at the "
                   "panel to resume.",
            .color = {200, 0, 0}, /* red */
            .active = false,
        },
};

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/* Transition flags - set on activation/clearance, consumed once by main.c. */
static bool s_newly_active[PANIC_COUNT];
static bool s_newly_cleared[PANIC_COUNT];

void panic_set(panic_id_t id) {
  if (id >= PANIC_COUNT)
    return;
  if (!s_panics[id].active) {
    s_panics[id].active = true;
    s_newly_active[id] = true;
  }
}

void panic_clear(panic_id_t id) {
  if (id >= PANIC_COUNT)
    return;
  if (s_panics[id].active) {
    s_newly_cleared[id] = true;
  }
  s_panics[id].active = false;
  s_newly_active[id] = false;
}

void panic_clear_all(void) {
  for (int i = 0; i < PANIC_COUNT; i++) {
    if (s_panics[i].active)
      s_newly_cleared[i] = true;
    s_panics[i].active = false;
    s_newly_active[i] = false;
  }
}

bool panic_any_active(void) {
  for (int i = 0; i < PANIC_COUNT; i++) {
    if (s_panics[i].active)
      return true;
  }
  return false;
}

const panic_entry_t *panic_display(void) {
  for (int i = 0; i < PANIC_COUNT; i++) {
    if (s_panics[i].active)
      return &s_panics[i];
  }
  return NULL;
}

const panic_entry_t *panic_consume_new(void) {
  for (int i = 0; i < PANIC_COUNT; i++) {
    if (s_newly_active[i]) {
      s_newly_active[i] = false;
      return &s_panics[i];
    }
  }
  return NULL;
}

const panic_entry_t *panic_consume_cleared(void) {
  for (int i = 0; i < PANIC_COUNT; i++) {
    if (s_newly_cleared[i]) {
      s_newly_cleared[i] = false;
      return &s_panics[i];
    }
  }
  return NULL;
}

panic_entry_t *panic_table(void) { return s_panics; }
