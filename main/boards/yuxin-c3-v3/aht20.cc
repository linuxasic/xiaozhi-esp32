#include "aht20.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Aht20"

#define AHT20_ADDR_7BIT     0x38
#define AHT20_DEV_WRITE     0x70
#define AHT20_DEV_READ      0x71

#define AHT20_CMD_INIT      0xBE
#define AHT20_CMD_MEASURE   0xAC
#define AHT20_CMD_RESET     0xBA

#define AHT20_INIT_PARAM    0x0800
#define AHT20_MEAS_PARAM    0x3300

Aht20::Aht20(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

bool Aht20::Initialize() {
    if (!i2c_device_) {
        ESP_LOGE(TAG, "I2C device handle is null");
        return false;
    }

    uint8_t init_buf[] = {AHT20_CMD_INIT, 0x08, 0x00};
    esp_err_t ret = i2c_master_transmit(i2c_device_, init_buf, sizeof(init_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send init command: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);

    uint8_t raw[6];
    ret = i2c_master_receive(i2c_device_, raw, 6, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(ret));
        return false;
    }

    if (!(raw[0] & 0x18)) {
        ESP_LOGE(TAG, "AHT20 not calibrated, status=0x%02X", raw[0]);
        return false;
    }

    ESP_LOGI(TAG, "AHT20 initialized successfully");
    return true;
}

bool Aht20::Read(float& temperature, float& humidity) {
    if (!i2c_device_) {
        ESP_LOGE(TAG, "I2C device handle is null");
        return false;
    }

    uint8_t meas_buf[] = {AHT20_CMD_MEASURE, 0x33, 0x00};
    esp_err_t ret = i2c_master_transmit(i2c_device_, meas_buf, sizeof(meas_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send measure command: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(80 / portTICK_PERIOD_MS);

    uint8_t raw[6];
    ret = i2c_master_receive(i2c_device_, raw, 6, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read data: %s", esp_err_to_name(ret));
        return false;
    }

    if (!(raw[0] & 0x18)) {
        ESP_LOGE(TAG, "AHT20 not calibrated, status=0x%02X", raw[0]);
        return false;
    }

    uint32_t raw_rh = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    uint32_t raw_temp = ((uint32_t)(raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

    humidity = raw_rh * 100.0f / (1 << 20);
    temperature = raw_temp * 200.0f / (1 << 20) - 50.0f;

    ESP_LOGD(TAG, "Temperature: %.1f C, Humidity: %.1f %%", temperature, humidity);
    return true;
}