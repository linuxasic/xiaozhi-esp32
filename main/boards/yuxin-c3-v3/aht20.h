#ifndef AHT20_H
#define AHT20_H

#include "i2c_device.h"

class Aht20 : public I2cDevice {
public:
    Aht20(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x38);
    bool Initialize();
    bool Read(float& temperature, float& humidity);

private:
    bool WaitForReady();
};

#endif // AHT20_H