#pragma once

#include <stdint.h>

/* Passive buzzer on GPIO46, driven by LEDC PWM at ~2700 Hz.
 * buzzer_beep() triggers a two-beep pattern; buzzer_update() must be
 * called once per main-loop tick (10 ms) to advance the pattern. */

#define BUZZER_GPIO 46

void buzzer_init(void);
void buzzer_beep(void);    /* trigger one beep sequence; ignored if already beeping */
void buzzer_update(void);  /* advance pattern state — call once per 10 ms tick      */
