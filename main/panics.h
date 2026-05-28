#pragma once

#include <stdbool.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
 * All possible panic conditions for the safety controller.
 *
 * To add a new fault:
 *   1. Add an entry to panic_id_t (before PANIC_COUNT).
 *   2. Add a row to s_panics[] in panics.c.
 *   3. Call panic_set() / panic_clear() from the subsystem that detects it.
 *
 * Array order determines display priority — earlier entries show first when
 * multiple faults are active simultaneously.
 * -------------------------------------------------------------------------- */

typedef enum {
    PANIC_ESTOP_HMI    = 0,  /* physical e-stop button at operator panel */
    PANIC_ZONE_SENSOR  = 1,  /* exclusion zone presence sensor           */
    PANIC_24V_LOST     = 2,  /* 24V PSU supply absent                    */
    PANIC_USB_LOST     = 3,  /* USB link to LinuxCNC dropped             */
    PANIC_SW_ESTOP     = 4,  /* software estop command from LinuxCNC     */
    PANIC_COUNT,
} panic_id_t;

typedef struct {
    uint8_t r, g, b;  /* logical RGB — GRB conversion happens in the LED driver */
} panic_rgb_t;

typedef struct {
    const char  *title;        /* short operator-facing fault name                   */
    const char  *source_type;  /* "GPIO", "USB", "SOFTWARE"                          */
    const char  *source_id;    /* "GPIO5", "CDC0", "LinuxCNC", etc.                  */
    const char  *explanation;  /* what happened and why the machine is stopped        */
    const char  *fix;          /* numbered steps to resolve, suitable for a display   */
    panic_rgb_t  color;        /* LED colour associated with this fault               */
    bool         active;       /* true while this fault condition is present          */
} panic_entry_t;

/* Set / clear individual faults */
void panic_set(panic_id_t id);
void panic_clear(panic_id_t id);
void panic_clear_all(void);

/* Query */
bool                  panic_any_active(void);
const panic_entry_t  *panic_display(void);        /* highest-priority active entry, or NULL */

/* Returns the next fault that became active since the last call, or NULL.
 * Call in a loop each tick to drain all newly-triggered faults — this is
 * the correct way to emit telemetry so a second fault while already in
 * PANIC is not silently dropped. */
const panic_entry_t  *panic_consume_new(void);

/* Returns the next fault that was cleared since the last call, or NULL.
 * Call in a loop each tick alongside panic_consume_new. */
const panic_entry_t  *panic_consume_cleared(void);

/* Direct table access for iteration (length is PANIC_COUNT) */
panic_entry_t *panic_table(void);
