#include "aht20.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Aht20"

#define AHT20_ADDR_7BIT     0x38

#define AHT20_CMD_INIT      0xBE
#define AHT20_CMD_MEASURE   0xAC
#define AHT20_CMD_RESET     0xBA

/* AHT20 status register bits */
#define AHT20_STATUS_BUSY       (1 << 7)
#define AHT20_STATUS_MODE       (1 << 6)
#define AHT20_STATUS_CALIBRATED (1 << 4)   /* Bit 4: 1=calibrated, 0=uncalibrated */

/* Measurement timeout */
#define AHT20_MEASURE_RETRIES    3
#define AHT20_MEASURE_DELAY_MS   100
#define AHT20_INIT_DELAY_MS      50

Aht20::Aht20(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

bool Aht20::Initialize() {
    if (!i2c_device_) {
        ESP_LOGE(TAG, "I2C device handle is null");
        return false;
    }

    // Send initialization command: 0xBE, 0x08, 0x00
    // This enables the sensor and enables calibration
    uint8_t init_buf[] = {AHT20_CMD_INIT, 0x08, 0x00};
    esp_err_t ret = i2c_master_transmit(i2c_device_, init_buf, sizeof(init_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send init command: %s", esp_err_to_name(ret));
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(AHT20_INIT_DELAY_MS));

    // Read status register to verify calibration
    uint8_t raw[6];
    ret = i2c_master_receive(i2c_device_, raw, 6, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(ret));
        return false;
    }

    // Check calibration bit (Bit 4), not using reserved bits
    if (!(raw[0] & AHT20_STATUS_CALIBRATED)) {
        ESP_LOGE(TAG, "AHT20 not calibrated, status=0x%02X", raw[0]);
        return false;
    }

    ESP_LOGI(TAG, "AHT20 initialized successfully (status=0x%02X)", raw[0]);
    return true;
}

bool Aht20::Read(float& temperature, float& humidity) {
    if (!i2c_device_) {
        ESP_LOGE(TAG, "I2C device handle is null");
        return false;
    }

    for (int retry = 0; retry < AHT20_MEASURE_RETRIES; retry++) {
        // Send measurement command: 0xAC, 0x33, 0x00
        uint8_t meas_buf[] = {AHT20_CMD_MEASURE, 0x33, 0x00};
        esp_err_t ret = i2c_master_transmit(i2c_device_, meas_buf, sizeof(meas_buf), 100);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send measure command (attempt %d): %s", retry + 1, esp_err_to_name(ret));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(AHT20_MEASURE_DELAY_MS));

        uint8_t raw[6];
        ret = i2c_master_receive(i2c_device_, raw, 6, 100);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read data (attempt %d): %s", retry + 1, esp_err_to_name(ret));
            continue;
        }

        // Check busy bit: if busy, wait and retry
        if (raw[0] & AHT20_STATUS_BUSY) {
            ESP_LOGW(TAG, "AHT20 busy (attempt %d), retrying...", retry + 1);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Check calibration bit
        if (!(raw[0] & AHT20_STATUS_CALIBRATED)) {
            ESP_LOGE(TAG, "AHT20 lost calibration, status=0x%02X", raw[0]);
            return false;
        }

        // Extract raw humidity (20 bits) and temperature (20 bits)
        uint32_t raw_rh = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
        uint32_t raw_temp = ((uint32_t)(raw[3] & 0x0F) << 16) | ((uint32_t)raw[4] << 8) | raw[5];

        humidity = raw_rh * 100.0f / (1 << 20);
        temperature = raw_temp * 200.0f / (1 << 20) - 50.0f;

        ESP_LOGD(TAG, "Temperature: %.1f C, Humidity: %.1f %%", temperature, humidity);
        return true;
    }

    ESP_LOGE(TAG, "Failed to read AHT20 after %d attempts", AHT20_MEASURE_RETRIES);
    return false;
}