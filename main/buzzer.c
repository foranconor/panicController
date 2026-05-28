#include "buzzer.h"
#include "driver/ledc.h"
#include <stdbool.h>
#include <stddef.h>

#define BUZZER_FREQ_HZ    2000
#define BUZZER_SPEED_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER      LEDC_TIMER_0
#define BUZZER_CHANNEL    LEDC_CHANNEL_0
#define BUZZER_RESOLUTION LEDC_TIMER_10_BIT
#define BUZZER_DUTY_25PCT 256  /* 25% of 2^10 — softer, less alarm-like */

/* Beep pattern: descending durations give a chime-like feel rather than
 * a mechanical double-beep. On/off periods are all different lengths so
 * the sound feels natural rather than clock-regular. */
static const struct { bool on; uint8_t ticks; } PATTERN[] = {
    { true,  8 },  /*  80 ms on  */
    { false, 6 },  /*  60 ms off */
    { true,  4 },  /*  40 ms on  */
};
#define PATTERN_LEN ((int)(sizeof(PATTERN) / sizeof(PATTERN[0])))

static int s_step      = -1;  /* -1 = idle */
static int s_remaining =  0;

static void set_output(bool on) {
    ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, on ? BUZZER_DUTY_25PCT : 0);
    ledc_update_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL);
}

void buzzer_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode      = BUZZER_SPEED_MODE,
        .duty_resolution = BUZZER_RESOLUTION,
        .timer_num       = BUZZER_TIMER,
        .freq_hz         = BUZZER_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = BUZZER_SPEED_MODE,
        .channel    = BUZZER_CHANNEL,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

void buzzer_beep(void) {
    if (s_step >= 0) return;  /* already beeping — don't restart */
    s_step = 0;
    s_remaining = PATTERN[0].ticks;
    set_output(PATTERN[0].on);
}

void buzzer_update(void) {
    if (s_step < 0) return;

    if (--s_remaining > 0) return;

    s_step++;
    if (s_step >= PATTERN_LEN) {
        s_step = -1;
        set_output(false);
        return;
    }

    s_remaining = PATTERN[s_step].ticks;
    set_output(PATTERN[s_step].on);
}
