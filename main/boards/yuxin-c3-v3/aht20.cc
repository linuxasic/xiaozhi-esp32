#include "aht20.h"

#include <esp_log.h>
#include <driver/i2c_master.h>

#define TAG "Aht20"

#define AHT20_CMD_INIT      0xBE
#define AHT20_CMD_MEASURE   0xAC
#define AHT20_CMD_RESET     0xBA

Aht20::Aht20(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
}

bool Aht20::Initialize() {
    uint8_t init_cmd[] = {AHT20_CMD_INIT, 0x08, 0x00};
    ESP_ERROR_CHECK(i2c_master_transmit(i2c_device_, init_cmd, sizeof(init_cmd), 100));

    vTaskDelay(100 / portTICK_PERIOD_MS);

    uint8_t status = ReadReg(0x00);
    if (!(status & 0x18)) {
        ESP_LOGE(TAG, "AHT20 not calibrated");
        return false;
    }

    ESP_LOGI(TAG, "AHT20 initialized successfully");
    return true;
}

bool Aht20::WaitForReady() {
    int timeout = 100;
    while (timeout--) {
        uint8_t status = ReadReg(0x00);
        if (!(status & 0x80)) {
            return true;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    ESP_LOGE(TAG, "AHT20 wait ready timeout");
    return false;
}

bool Aht20::Read(float& temperature, float& humidity) {
    uint8_t measure_cmd[] = {AHT20_CMD_MEASURE, 0x33, 0x00};
    ESP_ERROR_CHECK(i2c_master_transmit(i2c_device_, measure_cmd, sizeof(measure_cmd), 100));

    if (!WaitForReady()) {
        return false;
    }

    uint8_t data[7];
    ReadRegs(0x00, data, 7);

    if (!(data[0] & 0x18)) {
        ESP_LOGE(TAG, "AHT20 not calibrated");
        return false;
    }

    uint32_t humidity_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((data[3] & 0xF0) >> 4);
    uint32_t temp_raw = (((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5]);

    humidity = (humidity_raw / 1048576.0f) * 100.0f;
    temperature = (temp_raw / 1048576.0f) * 200.0f - 50.0f;

    ESP_LOGD(TAG, "Temperature: %.1f C, Humidity: %.1f %%", temperature, humidity);
    return true;
}