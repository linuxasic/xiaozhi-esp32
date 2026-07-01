#include "alarm_task.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/time.h>

#define TAG "AlarmTask"

static TaskHandle_t alarm_task_handle_ = nullptr;
static AlarmManager* alarm_manager_ = nullptr;

static void alarm_check_task(void* arg) {
    alarm_manager_ = static_cast<AlarmManager*>(arg);
    ESP_LOGI(TAG, "Alarm check task started");
    
    while (true) {
        timeval tv;
        gettimeofday(&tv, nullptr);
        
        struct tm tm_info;
        localtime_r(&tv.tv_sec, &tm_info);
        
        int current_hour = tm_info.tm_hour;
        int current_minute = tm_info.tm_min;
        int current_second = tm_info.tm_sec;
        int current_day = tm_info.tm_wday;
        
        if (current_second == 0) {
            auto alarms = alarm_manager_->GetAlarms();
            for (const auto& alarm : alarms) {
                if (alarm.enabled && alarm.hour == current_hour && alarm.minute == current_minute) {
                    bool should_trigger = true;
                    
                    if (alarm.repeat > 0) {
                        uint8_t repeat_mask = static_cast<uint8_t>(alarm.repeat);
                        uint8_t day_bit = 1 << current_day;
                        should_trigger = (repeat_mask & day_bit) != 0;
                    }
                    
                    if (should_trigger) {
                        ESP_LOGI(TAG, "Alarm triggered: %d %02d:%02d", alarm.id, alarm.hour, alarm.minute);
                        
                        alarm_manager_->TriggerAlarm(alarm);
                        
                        if (alarm.repeat == 0) {
                            alarm_manager_->RemoveAlarm(alarm.id);
                            ESP_LOGI(TAG, "One-time alarm %d triggered and auto-deleted", alarm.id);
                        }
                    }
                }
            }
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void StartAlarmCheckTask(AlarmManager* alarm_manager) {
    if (alarm_task_handle_ == nullptr) {
        xTaskCreate(alarm_check_task, "alarm_check", 4096, alarm_manager, 5, &alarm_task_handle_);
        ESP_LOGI(TAG, "Alarm check task created");
    }
}

void StopAlarmCheckTask() {
    if (alarm_task_handle_ != nullptr) {
        vTaskDelete(alarm_task_handle_);
        alarm_task_handle_ = nullptr;
        ESP_LOGI(TAG, "Alarm check task stopped");
    }
}
