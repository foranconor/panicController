#include "usb_serial.h"

#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include <string.h>

/* Small buffer for assembling incoming lines */
static char    s_rx_buf[64];
static uint8_t s_rx_pos = 0;

void usb_serial_init(void)
{
    tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .task = {
            .size     = 4096,
            .priority = 5,
            .xCoreID  = 0,
        },
    };
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port                    = TINYUSB_CDC_ACM_0,
        .callback_rx                 = NULL,
        .callback_rx_wanted_char     = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
}

bool usb_serial_connected(void)
{
    /* tud_mounted:       USB bus enumerated — clears on physical cable pull
     * tud_cdc_connected: DTR asserted — clears when OS closes the port
     * Need both: cable pull kills the bus but leaves DTR latched; process
     * death clears DTR but leaves the bus enumerated. */
    return tud_mounted() && tud_cdc_connected();
}

void usb_serial_send(const char *msg)
{
    if (!tud_mounted()) return;

    const uint8_t *data      = (const uint8_t *)msg;
    size_t         remaining = strlen(msg);

    while (remaining > 0) {
        size_t written = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, remaining);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        if (written == 0) break;  /* USB stalled — drop rather than spin */
        data      += written;
        remaining -= written;
    }
}

bool usb_serial_readline(char *buf, size_t maxlen)
{
    uint32_t avail = tud_cdc_available();
    while (avail > 0) {
        char c;
        tud_cdc_read(&c, 1);
        avail--;

        if (c == '\n' || c == '\r') {
            if (s_rx_pos > 0) {
                size_t n = s_rx_pos < maxlen - 1 ? s_rx_pos : maxlen - 1;
                memcpy(buf, s_rx_buf, n);
                buf[n]   = '\0';
                s_rx_pos = 0;
                return true;
            }
        } else if (s_rx_pos < sizeof(s_rx_buf) - 1) {
            s_rx_buf[s_rx_pos++] = c;
        }
    }
    return false;
}
