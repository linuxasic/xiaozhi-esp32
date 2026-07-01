#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/oled_display.h"
#include "application.h"
#include "button.h"
#include "led/single_led.h"
#include "mcp_server.h"
#include "settings.h"
#include "config.h"
#include "power_save_timer.h"
#include "adc_battery_monitor.h"
#include "press_to_talk_mcp_tool.h"
#include "aht20.h"
#include "alarm_manager.h"
#include "alarm_task.h"
#include "assets/lang_config.h"

#include <wifi_manager.h>
#include <esp_log.h>
#include <esp_efuse_table.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#define TAG "YuxinC3Board"

class YuxinC3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    PowerSaveTimer* power_save_timer_ = nullptr;
    AdcBatteryMonitor* adc_battery_monitor_ = nullptr;
    PressToTalkMcpTool* press_to_talk_tool_ = nullptr;
    Aht20* aht20_ = nullptr;
    float last_temperature_ = 0.0f;
    float last_humidity_ = 0.0f;
        AlarmManager* alarm_manager_ = nullptr;
    
    // 闹钟响铃状态
    bool alarm_ringing_ = false;
    int ringing_alarm_id_ = -1;
    esp_timer_handle_t alarm_beep_timer_ = nullptr;

    void StopAlarmRinging() {
        if (alarm_ringing_) {
            alarm_ringing_ = false;
            ringing_alarm_id_ = -1;
            if (alarm_beep_timer_) {
                esp_timer_stop(alarm_beep_timer_);
            }
            ESP_LOGI(TAG, "Alarm ringing stopped by user");
        }
    }

    void InitializePowerManager() {
        adc_battery_monitor_ = new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_3, 100000, 100000, GPIO_NUM_12);
        adc_battery_monitor_->OnChargingStatusChanged([this](bool is_charging) {
            if (is_charging) {
                power_save_timer_->SetEnabled(false);
            } else {
                power_save_timer_->SetEnabled(true);
            }
        });
    }

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(160, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(true);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));

        if (i2c_master_probe(codec_i2c_bus_, 0x18, 1000) != ESP_OK) {
            while (true) {
                ESP_LOGE(TAG, "Failed to probe I2C bus, please check if you have installed the correct firmware");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }
        }
    }

    void InitializeSsd1306Display() {
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(codec_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
        ESP_LOGI(TAG, "SSD1306 driver installed");

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }

        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

        void InitializeButtons() {
        boot_button_.OnClick([this]() {
            // 闹钟响铃时，按任意键停止响铃
            if (alarm_ringing_) {
                StopAlarmRinging();
                
                Application::GetInstance().Schedule([this]() {
                    auto display = Board::GetInstance().GetDisplay();
                    display->SetEmotion("neutral");
                    display->ShowNotification("闹钟已关闭", 2000);
                });
                return;
            }
            
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            if (!press_to_talk_tool_ || !press_to_talk_tool_->IsPressToTalkEnabled()) {
                app.ToggleChatState();
            }
        });
        boot_button_.OnPressDown([this]() {
            if (power_save_timer_) {
                power_save_timer_->WakeUp();
            }
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                Application::GetInstance().StartListening();
            }
        });
        boot_button_.OnPressUp([this]() {
            if (press_to_talk_tool_ && press_to_talk_tool_->IsPressToTalkEnabled()) {
                Application::GetInstance().StopListening();
            }
        });
    }

    void InitializeAht20() {
        aht20_ = new Aht20(codec_i2c_bus_, 0x38);
        if (!aht20_->Initialize()) {
            ESP_LOGE(TAG, "AHT20 initialization failed");
            delete aht20_;
            aht20_ = nullptr;
        } else {
            ESP_LOGI(TAG, "AHT20 initialized successfully");
        }
    }

    void InitializeTools() {
        press_to_talk_tool_ = new PressToTalkMcpTool();
        press_to_talk_tool_->Initialize();

        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.sensor.get_temperature_humidity",
            "Get the current temperature and humidity from the AHT20 sensor.\n"
            "Use this tool when the user asks about temperature or humidity.\n"
            "Return:\n"
            "  A JSON object with 'temperature' (in Celsius) and 'humidity' (in percent).",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                float temp, humi;
                if (aht20_ && aht20_->Read(temp, humi)) {
                    last_temperature_ = temp;
                    last_humidity_ = humi;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "温度: %.1f°C, 湿度: %.1f%%", temp, humi);
                    GetDisplay()->ShowNotification(buf, 3000);
                    return std::string(buf);
                } else {
                    return std::string("无法读取温湿度数据");
                }
            });

        mcp_server.AddTool("self.alarm.set",
            "Set an alarm.\n"
            "Parameters:\n"
            "  hour: Hour (0-23)\n"
            "  minute: Minute (0-59)\n"
            "  message: Optional message to display when alarm triggers.\n"
            "  repeat: Repeat pattern (0=once, 1=Monday, 2=Tuesday, 4=Wednesday, 8=Thursday, 16=Friday, 32=Saturday, 64=Sunday).\n"
            "Return:\n"
            "  Alarm ID if successful, error message otherwise.",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 0, 0, 23),
                Property("minute", kPropertyTypeInteger, 0, 0, 59),
                Property("message", kPropertyTypeString, ""),
                Property("repeat", kPropertyTypeInteger, 0, 0, 127)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int hour = properties["hour"].value<int>();
                int minute = properties["minute"].value<int>();
                std::string message = properties["message"].value<std::string>();
                int repeat = properties["repeat"].value<int>();
                
                if (alarm_manager_->AddAlarm(hour, minute, message, repeat)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "闹钟已设置: %02d:%02d", hour, minute);
                    GetDisplay()->ShowNotification(buf, 3000);
                    return std::string(buf);
                } else {
                    return std::string("设置闹钟失败");
                }
            });

        mcp_server.AddTool("self.alarm.list",
            "List all alarms.\n"
            "Return:\n"
            "  JSON array of alarms with id, hour, minute, enabled, message, and repeat.",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto alarms = alarm_manager_->GetAlarms();
                if (alarms.empty()) {
                    return std::string("暂无闹钟");
                }
                
                cJSON* root = cJSON_CreateArray();
                for (const auto& alarm : alarms) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddNumberToObject(item, "id", alarm.id);
                    cJSON_AddNumberToObject(item, "hour", alarm.hour);
                    cJSON_AddNumberToObject(item, "minute", alarm.minute);
                    cJSON_AddBoolToObject(item, "enabled", alarm.enabled);
                    cJSON_AddStringToObject(item, "message", alarm.message.c_str());
                    cJSON_AddNumberToObject(item, "repeat", alarm.repeat);
                    cJSON_AddItemToArray(root, item);
                }
                
                char* buf = cJSON_PrintUnformatted(root);
                std::string result(buf);
                cJSON_free(buf);
                cJSON_Delete(root);
                return result;
            });

        mcp_server.AddTool("self.alarm.delete",
            "Delete an alarm by ID.\n"
            "Parameters:\n"
            "  id: Alarm ID to delete.\n"
            "Return:\n"
            "  Success message if deleted, error otherwise.",
            PropertyList({
                Property("id", kPropertyTypeInteger, 0, 0, 9999)
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                int id = properties["id"].value<int>();
                
                if (alarm_manager_->RemoveAlarm(id)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "闹钟 %d 已删除", id);
                    GetDisplay()->ShowNotification(buf, 3000);
                    return std::string(buf);
                } else {
                    return std::string("删除闹钟失败，未找到该闹钟");
                }
            });
    }

public:
    YuxinC3Board() : boot_button_(BOOT_BUTTON_GPIO, false, 0, 0, true) {  
        InitializePowerManager();
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        InitializeAht20();
        InitializeSsd1306Display();
        InitializeButtons();
        
                                alarm_manager_ = new AlarmManager();
        
        // 创建闹钟响铃定时器，每 3 秒播放一次提示音，直到用户按键停止
        esp_timer_create_args_t beep_timer_args = {
            .callback = [](void* arg) {
                auto* self = static_cast<YuxinC3Board*>(arg);
                if (self->alarm_ringing_) {
                    // 通过 Schedule 切换到主任务上下文播放声音
                    Application::GetInstance().Schedule([]() {
                        Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                    });
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "alarm_beep",
            .skip_unhandled_events = true
        };
        ESP_ERROR_CHECK(esp_timer_create(&beep_timer_args, &alarm_beep_timer_));
        
        alarm_manager_->SetAlarmCallback([this](const Alarm& alarm) {
            char buf[128];
            if (alarm.message.empty()) {
                snprintf(buf, sizeof(buf), "闹钟时间到: %02d:%02d", alarm.hour, alarm.minute);
            } else {
                snprintf(buf, sizeof(buf), "%s: %02d:%02d", alarm.message.c_str(), alarm.hour, alarm.minute);
            }
            
            // 显示通知到屏幕
            GetDisplay()->ShowNotification(buf, 5000);
            
            // 记录当前响铃的闹钟 ID
            ringing_alarm_id_ = alarm.id;
            
            // 通过 Schedule 在主任务中设置状态并开始循环播放
            Application::GetInstance().Schedule([this, message = std::string(buf)]() {
                auto display = Board::GetInstance().GetDisplay();
                
                // 设置闹钟响铃标志
                alarm_ringing_ = true;
                
                // 显示闹钟表情
                display->SetEmotion("alarm_clock");
                
                // 立即播放一次提示音
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
                
                // 启动定时器循环播放（每 3 秒一次）
                if (alarm_beep_timer_) {
                    esp_timer_start_periodic(alarm_beep_timer_, 3000000); // 3秒
                }
            });
            
            ESP_LOGI(TAG, "Alarm triggered: %s", buf);
        });
        StartAlarmCheckTask(alarm_manager_);
        
        InitializeTools();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_0, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual std::string GetDeviceStatusJson() override {
        std::string json = WifiBoard::GetDeviceStatusJson();
        cJSON* root = cJSON_Parse(json.c_str());

        if (aht20_) {
            float temp, humi;
            if (aht20_->Read(temp, humi)) {
                last_temperature_ = temp;
                last_humidity_ = humi;
            }
        }

        cJSON* sensor = cJSON_CreateObject();
        cJSON_AddNumberToObject(sensor, "temperature", last_temperature_);
        cJSON_AddNumberToObject(sensor, "humidity", last_humidity_);
        cJSON_AddItemToObject(root, "sensor", sensor);

        char* str = cJSON_PrintUnformatted(root);
        std::string result(str);
        cJSON_free(str);
        cJSON_Delete(root);
        return result;
    }
};

DECLARE_BOARD(YuxinC3Board);