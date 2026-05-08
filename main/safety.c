#include "safety.h"
#include "panics.h"

#include "driver/gpio.h"

#define GLITCH_THRESHOLD_LOOPS  (GLITCH_THRESHOLD_MS / LOOP_PERIOD_MS)
#define CALM_SWEEP_LOOPS        (CALM_SWEEP_MS       / LOOP_PERIOD_MS)
#define CLEAR_SETTLE_LOOPS      (CLEAR_SETTLE_MS     / LOOP_PERIOD_MS)

/* --------------------------------------------------------------------------
 * GPIO wiring manifest
 *
 * Maps physical pins to panic IDs. danger_level is the GPIO level that
 * means the fault condition is present.
 *
 * Inputs use pull-down, so a disconnected/broken wire reads LOW.
 * danger_level = false → LOW = danger (fail-safe: wire break triggers fault).
 * -------------------------------------------------------------------------- */

typedef struct {
    int        gpio;
    bool       danger_level;
    panic_id_t panic_id;
} gpio_input_t;

static const gpio_input_t s_gpio_inputs[] = {
    {SAFETY_ZONE_GPIO,  false, PANIC_ZONE_SENSOR},
    {SAFETY_ESTOP_GPIO, false, PANIC_ESTOP_HMI},
};
#define NUM_GPIO_INPUTS  ((int)(sizeof(s_gpio_inputs) / sizeof(s_gpio_inputs[0])))

/* Glitch filter counters — one per GPIO input, mutable runtime state */
static uint32_t s_consec[NUM_GPIO_INPUTS];

/* --------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------- */

static safety_state_t s_state       = SAFETY_PANIC;
static uint32_t       s_glitches    = 0;
static uint32_t       s_calm_count  = 0;
static uint32_t       s_settle_count = 0;
static bool           s_prev_usb    = false;
static bool           s_usb_faulted = false;  /* latched on USB drop, cleared on reconnect */

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

void safety_init(void)
{
    /* Safety inputs: pull-down, LOW = danger (fail-safe). */
    for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask  = (1ULL << s_gpio_inputs[i].gpio),
            .mode          = GPIO_MODE_INPUT,
            .pull_up_en    = GPIO_PULLUP_DISABLE,
            .pull_down_en  = GPIO_PULLDOWN_ENABLE,
            .intr_type     = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    /* Ack button: active low, pull-up. The only path out of PANIC/CLEAR. */
    gpio_config_t ack_cfg = {
        .pin_bit_mask  = (1ULL << SAFETY_ACK_GPIO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&ack_cfg);

    /* Safety output: driven LOW until the state machine reaches OK. */
    gpio_config_t out_cfg = {
        .pin_bit_mask  = (1ULL << SAFETY_OUTPUT_GPIO),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(SAFETY_OUTPUT_GPIO, 0);
}

/* --------------------------------------------------------------------------
 * Update — call once per loop tick
 * -------------------------------------------------------------------------- */

safety_state_t safety_update(bool usb_connected)
{
    /* Drive safety output — reflects state from the previous tick.
     * Writing here means hardware changes before any USB send this loop. */
    gpio_set_level(SAFETY_OUTPUT_GPIO, (s_state == SAFETY_OK) ? 1 : 0);

    /* Read ack button. Active low — GPIO low = button pressed. */
    bool ack_pressed = (gpio_get_level(SAFETY_ACK_GPIO) == 0);

    /* Scan GPIO inputs. Pulses shorter than GLITCH_THRESHOLD are discarded.
     * Sustained faults set the corresponding panic; returning to safe clears it.
     * gpio_triggered is true if any input is currently at or past the threshold. */
    bool gpio_triggered = false;
    for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
        int  raw       = gpio_get_level(s_gpio_inputs[i].gpio);
        bool is_high   = (raw != 0);
        bool is_danger = (is_high == s_gpio_inputs[i].danger_level);

        if (is_danger) {
            s_consec[i]++;
            if (s_consec[i] >= GLITCH_THRESHOLD_LOOPS) {
                panic_set(s_gpio_inputs[i].panic_id);
                gpio_triggered = true;
            }
        } else {
            if (s_consec[i] > 0 && s_consec[i] < GLITCH_THRESHOLD_LOOPS) {
                s_glitches++;
            }
            panic_clear(s_gpio_inputs[i].panic_id);
            s_consec[i] = 0;
        }
    }

    /* Detect USB drop as a falling edge. */
    bool usb_just_dropped = (s_prev_usb && !usb_connected);
    s_prev_usb = usb_connected;

    /* State machine */
    if (s_state == SAFETY_PANIC) {

        /* USB reconnect releases the USB fault latch. */
        if (usb_connected && s_usb_faulted) {
            s_usb_faulted = false;
            panic_clear(PANIC_USB_LOST);
        }

        bool can_clear = !gpio_triggered && !s_usb_faulted && usb_connected;
        if (can_clear) {
            panic_clear(PANIC_SW_ESTOP);
            s_state = SAFETY_CLEAR;
            s_calm_count = 0;
        }

    } else if (s_state == SAFETY_CLEAR) {

        if (s_calm_count < CALM_SWEEP_LOOPS) {
            s_calm_count++;
        }

        if (gpio_triggered) {
            s_state = SAFETY_PANIC;
        } else if (usb_just_dropped) {
            s_usb_faulted = true;
            panic_set(PANIC_USB_LOST);
            s_state = SAFETY_PANIC;
        } else if (ack_pressed) {
            s_state = SAFETY_READY;
            s_settle_count = 0;
        }

    } else if (s_state == SAFETY_READY) {

        if (gpio_triggered) {
            s_state = SAFETY_PANIC;
            s_settle_count = 0;
        } else if (usb_just_dropped) {
            s_usb_faulted = true;
            panic_set(PANIC_USB_LOST);
            s_state = SAFETY_PANIC;
            s_settle_count = 0;
        } else if (s_settle_count >= CLEAR_SETTLE_LOOPS) {
            s_state = SAFETY_OK;
        } else {
            s_settle_count++;
        }

    } else if (s_state == SAFETY_OK) {

        if (gpio_triggered) {
            s_state = SAFETY_PANIC;
        } else if (!usb_connected) {
            s_usb_faulted = true;
            panic_set(PANIC_USB_LOST);
            s_state = SAFETY_PANIC;
        }

    } else {
        /* Unknown state — should never happen. Safe default. */
        panic_set(PANIC_SW_ESTOP);
        s_state = SAFETY_PANIC;
    }

    return s_state;
}

/* --------------------------------------------------------------------------
 * Force a software estop (LinuxCNC sent ESTOP over USB)
 * -------------------------------------------------------------------------- */

void safety_force_estop(void)
{
    s_state = SAFETY_PANIC;
    panic_set(PANIC_SW_ESTOP);
    s_settle_count = 0;
    for (int i = 0; i < NUM_GPIO_INPUTS; i++) {
        s_consec[i] = 0;
    }
}

/* --------------------------------------------------------------------------
 * Accessors
 * -------------------------------------------------------------------------- */

uint32_t safety_calm_progress(void)   { return s_calm_count;   }
uint32_t safety_settle_progress(void) { return s_settle_count; }

uint32_t safety_get_and_reset_glitches(void)
{
    uint32_t g = s_glitches;
    s_glitches = 0;
    return g;
}
