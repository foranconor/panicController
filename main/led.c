#include "led.h"
#include "panics.h"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include <stdlib.h>

#define LED_RMT_RES_HZ 10000000 /* 10 MHz → 100 ns resolution */

/* SK6812/WS2812 timing in ticks at 10 MHz (100 ns per tick) */
#define T0H 4   /* 400 ns */
#define T0L 8   /* 800 ns */
#define T1H 8   /* 800 ns */
#define T1L 4   /* 400 ns */
#define RST 500 /* 50 µs reset */

static rmt_channel_handle_t s_chan = NULL;
static rmt_encoder_handle_t s_encoder = NULL;

typedef struct {
  rmt_encoder_t base;
  rmt_encoder_t *copy_encoder;
  rmt_encoder_t *bytes_encoder;
  int state;
} led_encoder_t;

static size_t IRAM_ATTR led_encode(rmt_encoder_t *encoder,
                                   rmt_channel_handle_t channel,
                                   const void *primary_data, size_t data_size,
                                   rmt_encode_state_t *ret_state) {
  led_encoder_t *enc = __containerof(encoder, led_encoder_t, base);
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  size_t encoded = 0;

  if (enc->state == 0) {
    encoded += enc->bytes_encoder->encode(
        enc->bytes_encoder, channel, primary_data, data_size, &session_state);
    if (session_state & RMT_ENCODING_COMPLETE) {
      enc->state = 1;
      session_state &= ~RMT_ENCODING_COMPLETE;
    }
    if (session_state & RMT_ENCODING_MEM_FULL) {
      *ret_state = session_state;
      return encoded;
    }
  }

  if (enc->state == 1) {
    static const rmt_symbol_word_t reset_sym = {
        .level0 = 0,
        .duration0 = RST,
        .level1 = 0,
        .duration1 = RST,
    };
    encoded += enc->copy_encoder->encode(enc->copy_encoder, channel, &reset_sym,
                                         sizeof(reset_sym), &session_state);
    if (session_state & RMT_ENCODING_COMPLETE)
      enc->state = 0;
  }

  *ret_state = session_state;
  return encoded;
}

static esp_err_t led_encoder_del(rmt_encoder_t *encoder) {
  led_encoder_t *enc = __containerof(encoder, led_encoder_t, base);
  rmt_del_encoder(enc->copy_encoder);
  rmt_del_encoder(enc->bytes_encoder);
  free(enc);
  return ESP_OK;
}

static esp_err_t led_encoder_reset(rmt_encoder_t *encoder) {
  led_encoder_t *enc = __containerof(encoder, led_encoder_t, base);
  rmt_encoder_reset(enc->copy_encoder);
  rmt_encoder_reset(enc->bytes_encoder);
  enc->state = 0;
  return ESP_OK;
}

void led_init(void) {
  rmt_tx_channel_config_t chan_cfg = {
      .gpio_num = LED_GPIO,
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = LED_RMT_RES_HZ,
      .mem_block_symbols = 64,
      .trans_queue_depth = 4,
  };
  if (rmt_new_tx_channel(&chan_cfg, &s_chan) != ESP_OK) {
    s_chan = NULL;
    return;
  }

  led_encoder_t *enc = calloc(1, sizeof(led_encoder_t));
  enc->base.encode = led_encode;
  enc->base.del = led_encoder_del;
  enc->base.reset = led_encoder_reset;

  rmt_copy_encoder_config_t copy_cfg = {};
  rmt_new_copy_encoder(&copy_cfg, &enc->copy_encoder);

  rmt_bytes_encoder_config_t bytes_cfg = {
      .bit0 = {.level0 = 1, .duration0 = T0H, .level1 = 0, .duration1 = T0L},
      .bit1 = {.level0 = 1, .duration0 = T1H, .level1 = 0, .duration1 = T1L},
      .flags.msb_first = 1,
  };
  rmt_new_bytes_encoder(&bytes_cfg, &enc->bytes_encoder);

  s_encoder = &enc->base;
  rmt_enable(s_chan);
}

void led_set_grb(uint8_t g, uint8_t r, uint8_t b) {
  if (!s_chan || !s_encoder)
    return;
  uint8_t buf[3] = {g, r, b};
  rmt_transmit_config_t tx_cfg = {.loop_count = 0};
  rmt_transmit(s_chan, s_encoder, buf, sizeof(buf), &tx_cfg);
  rmt_tx_wait_all_done(s_chan, 10);
}

/*
 * LED state colours:
 *
 *   PANIC  — 2 Hz alternation: magenta/crimson ↔ combined panic colour.
 *            Panic colours are summed per channel (clamped to 255), so
 *            multiple simultaneous faults blend toward white.
 *   CLEAR  — purple→cyan sweep, then breathing cyan  (waiting for ack)
 *   READY  — cyan→lime sweep  (inputs settling after ack)
 *   OK     — steady green + lime flash every 2 s  (machine may run)
 *   OK + no USB — slow yellow-orange blink  (transient: USB just dropped)
 */

#define PANIC_PHASE_TICKS (250 / LOOP_PERIOD_MS) /* 2 Hz: 250 ms per phase */

void led_update_state(safety_state_t safety, bool usb_ok, uint32_t tick) {
  switch (safety) {

  case SAFETY_PANIC: {
    /* Sum RGB channels across all active faults, clamp to 255.
     * One fault → its colour. Multiple faults → blends toward white. */
    uint16_t pr = 0, pg = 0, pb = 0;
    panic_entry_t *tbl = panic_table();
    for (int i = 0; i < PANIC_COUNT; i++) {
      if (tbl[i].active) {
        pr += tbl[i].color.r;
        pg += tbl[i].color.g;
        pb += tbl[i].color.b;
      }
    }
    if (pr > 255)
      pr = 255;
    if (pg > 255)
      pg = 255;
    if (pb > 255)
      pb = 255;

    /* Alternate: magenta/crimson base ↔ combined panic colour, 2 Hz */
    bool show_panic = ((tick % (PANIC_PHASE_TICKS * 2)) >= PANIC_PHASE_TICKS) &&
                      (pr || pg || pb);
    if (show_panic) {
      led_set_grb((uint8_t)pg, (uint8_t)pr, (uint8_t)pb);
    } else {
      led_set_grb(20, 220, 60); /* crimson base */
    }
    break;
  }

  case SAFETY_CLEAR: {
    uint32_t calm = safety_calm_progress();
    if (calm < (CALM_SWEEP_MS / LOOP_PERIOD_MS)) {
      /* Purple → cyan: red fades out, green rises slightly, blue holds */
      uint8_t t = (uint8_t)(calm * 255 / (CALM_SWEEP_MS / LOOP_PERIOD_MS));
      uint8_t r = (uint8_t)((uint16_t)(255 - t) * 200 / 255);
      uint8_t g = (uint8_t)((uint16_t)t * 80 / 255);
      uint8_t b = 200;
      led_set_grb(g, r, b);
    } else {
      /* Breathing cyan — waiting for operator ack button */
      uint32_t period = 100; /* 1000 ms */
      uint32_t phase = tick % period;
      uint8_t bv =
          (uint8_t)(phase < period / 2 ? phase * 255 / (period / 2)
                                       : (period - phase) * 255 / (period / 2));
      uint8_t b = (uint8_t)(60 + (uint16_t)bv * 140 / 255);
      uint8_t g = (uint8_t)((uint16_t)b * 80 / 255);
      led_set_grb(g, 0, b);
    }
    break;
  }

  case SAFETY_READY: {
    /* Cyan → lime sweep over 150 ms — arrives at warm lime when inputs
     * confirmed stable */
    uint32_t prog = safety_settle_progress();
    uint8_t t =
        (uint8_t)((uint32_t)prog * 255 / (CLEAR_SETTLE_MS / LOOP_PERIOD_MS));
    uint8_t g = (uint8_t)(80 + (uint16_t)t * 120 / 255);    /* 80  → 200 */
    uint8_t r = (uint8_t)((uint16_t)t * 80 / 255);          /*   0 →  80 */
    uint8_t b = (uint8_t)((uint16_t)(255 - t) * 200 / 255); /* 200 →   0 */
    led_set_grb(g, r, b);
    break;
  }

  case SAFETY_OK: {
    if (!usb_ok) {
      /* No computer connected — slow yellow-orange blink */
      if ((tick % SLOW_PULSE_TICKS) < (SLOW_PULSE_TICKS / 2))
        led_set_grb(180, 220, 0);
      else
        LED_OFF();
      break;
    }
    /* Steady green — brief lime flash at each heartbeat */
    uint32_t phase = tick % HEARTBEAT_INTERVAL;
    if (phase < 4)
      led_set_grb(100, 0, 0);
    else
      led_set_grb(50, 0, 0);
    break;
  }
  }
}
