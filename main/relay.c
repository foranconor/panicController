#include "relay.h"
#include "telemetry.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include <stdio.h>

#define TAG "relay"

#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_GPIO    42
#define I2C_SCL_GPIO    41
#define I2C_FREQ_HZ     100000

#define TCA9554_ADDR        0x20
#define TCA9554_REG_OUTPUT  0x01
#define TCA9554_REG_CONFIG  0x03

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_output = 0x00;  /* shadow register — all relays off */

static void write_output(void)
{
    uint8_t buf[2] = { TCA9554_REG_OUTPUT, s_output };
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(err));
    }
}

void relay_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_PORT,
        .sda_io_num          = I2C_SDA_GPIO,
        .scl_io_num          = I2C_SCL_GPIO,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TCA9554_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev));
    ESP_LOGI(TAG, "TCA9554 configured at 0x%02X", TCA9554_ADDR);

    /* Set all pins as outputs (config register: 0 = output) */
    uint8_t config_buf[2] = { TCA9554_REG_CONFIG, 0x00 };
    esp_err_t err = i2c_master_transmit(s_dev, config_buf, sizeof(config_buf), 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 config write failed: %s", esp_err_to_name(err));
        ESP_ERROR_CHECK(err);
    }

    /* Drive all relays off */
    s_output = 0x00;
    write_output();
}

void relay_set(relay_id_t id, bool on)
{
    uint8_t bit = (uint8_t)(1u << (uint8_t)id);
    uint8_t new_output = on ? (s_output | bit) : (s_output & ~bit);
    if (new_output == s_output) {
        return;  /* no change — skip I2C write */
    }
    s_output = new_output;
    write_output();
}

void relay_scan_i2c(uint32_t uptime_s)
{
    telemetry_event(uptime_s, "i2c_scan", "TCA9554 relay expander active");
}
