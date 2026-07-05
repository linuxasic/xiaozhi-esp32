#include "alarm_manager.h"

#include <algorithm>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>

#define TAG "AlarmManager"
#define NVS_NAMESPACE "alarms"

// Global recursive mutex for thread safety (allows same thread to lock multiple times)
std::recursive_mutex g_alarm_mutex;

std::string Alarm::ToJson() const {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "id", id);
    cJSON_AddNumberToObject(json, "hour", hour);
    cJSON_AddNumberToObject(json, "minute", minute);
    cJSON_AddBoolToObject(json, "enabled", enabled);
    cJSON_AddStringToObject(json, "message", message.c_str());
    cJSON_AddNumberToObject(json, "repeat", repeat);
    
    char* str = cJSON_PrintUnformatted(json);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(json);
    return result;
}

Alarm Alarm::FromJson(const std::string& json_str) {
    Alarm alarm;
    cJSON* json = cJSON_Parse(json_str.c_str());
    if (json) {
        alarm = FromCJsonObject(json);
        cJSON_Delete(json);
    }
    return alarm;
}

Alarm Alarm::FromCJsonObject(cJSON* json) {
    Alarm alarm;
    if (!json) return alarm;
    
    cJSON* id = cJSON_GetObjectItem(json, "id");
    cJSON* hour = cJSON_GetObjectItem(json, "hour");
    cJSON* minute = cJSON_GetObjectItem(json, "minute");
    cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
    cJSON* message = cJSON_GetObjectItem(json, "message");
    cJSON* repeat = cJSON_GetObjectItem(json, "repeat");
    
    if (id) alarm.id = id->valueint;
    if (hour) alarm.hour = hour->valueint;
    if (minute) alarm.minute = minute->valueint;
    if (enabled) alarm.enabled = enabled->valueint != 0;
    if (message && message->valuestring) alarm.message = message->valuestring;
    if (repeat) alarm.repeat = repeat->valueint;
    
    return alarm;
}

AlarmManager::AlarmManager() : next_id_(1) {
    LoadAlarms();
}

AlarmManager::~AlarmManager() {
    SaveAlarms();
}

bool AlarmManager::AddAlarm(int hour, int minute, const std::string& message, int repeat) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ESP_LOGE(TAG, "Invalid time: %d:%d", hour, minute);
        return false;
    }
    
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    Alarm alarm;
    alarm.id = next_id_++;
    alarm.hour = hour;
    alarm.minute = minute;
    alarm.enabled = true;
    alarm.message = message;
    alarm.repeat = repeat;
    
    alarms_.push_back(alarm);
    
    if (!SaveAlarms()) {
        // Rollback on save failure
        alarms_.pop_back();
        next_id_--;
        ESP_LOGE(TAG, "Failed to save alarm to NVS, rolled back");
        return false;
    }
    
    ESP_LOGI(TAG, "Added alarm: %d %02d:%02d %s", alarm.id, alarm.hour, alarm.minute, alarm.message.c_str());
    return true;
}

bool AlarmManager::RemoveAlarm(int id) {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(), [id](const Alarm& a) {
        return a.id == id;
    });
    
    if (it != alarms_.end()) {
        alarms_.erase(it);
        SaveAlarms();
        ESP_LOGI(TAG, "Removed alarm: %d", id);
        return true;
    }
    
    ESP_LOGE(TAG, "Alarm not found: %d", id);
    return false;
}

bool AlarmManager::EnableAlarm(int id, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(), [id](const Alarm& a) {
        return a.id == id;
    });
    
    if (it != alarms_.end()) {
        it->enabled = enabled;
        SaveAlarms();
        ESP_LOGI(TAG, "Alarm %d %s", id, enabled ? "enabled" : "disabled");
        return true;
    }
    
    ESP_LOGE(TAG, "Alarm not found: %d", id);
    return false;
}

std::vector<Alarm> AlarmManager::GetAlarms() const {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    return alarms_;
}

Alarm* AlarmManager::GetAlarm(int id) {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    auto it = std::find_if(alarms_.begin(), alarms_.end(), [id](const Alarm& a) {
        return a.id == id;
    });
    
    return (it != alarms_.end()) ? &(*it) : nullptr;
}

int AlarmManager::GetNextAlarmId() {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    return next_id_;
}

void AlarmManager::LoadAlarms() {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }
    
    size_t len = 0;
    err = nvs_get_str(handle, "list", nullptr, &len);
    if (err == ESP_OK && len > 0) {
        char buf[512];
        if (len > sizeof(buf)) {
            ESP_LOGW(TAG, "Alarm list too large: %zu bytes", len);
            nvs_close(handle);
            return;
        }
        err = nvs_get_str(handle, "list", buf, &len);
        if (err == ESP_OK) {
            cJSON* root = cJSON_Parse(buf);
            if (root && cJSON_IsArray(root)) {
                alarms_.clear();
                int size = cJSON_GetArraySize(root);
                for (int i = 0; i < size; i++) {
                    cJSON* item = cJSON_GetArrayItem(root, i);
                    if (item) {
                        Alarm alarm = Alarm::FromCJsonObject(item);
                        if (!alarm.enabled && alarm.repeat == 0) {
                            ESP_LOGI(TAG, "Skipping disabled one-time alarm %d during load", alarm.id);
                            continue;
                        }
                        alarms_.push_back(alarm);
                    }
                }
                ESP_LOGI(TAG, "Loaded %d alarms from NVS", alarms_.size());
            } else {
                ESP_LOGW(TAG, "Invalid JSON in NVS, starting fresh");
            }
            cJSON_Delete(root);
        } else {
            ESP_LOGW(TAG, "Failed to read NVS string: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS get string result: %s", esp_err_to_name(err));
    }
    
    int32_t saved_next_id = 0;
    err = nvs_get_i32(handle, "next_id", &saved_next_id);
    if (err == ESP_OK && saved_next_id > 0) {
        next_id_ = saved_next_id;
        ESP_LOGI(TAG, "Next alarm ID from NVS: %d", next_id_);
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Loaded %d alarms, next_id=%d", alarms_.size(), next_id_);
}

bool AlarmManager::SaveAlarms() {
    // Note: Caller must already hold g_alarm_mutex
    
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }
    
    cJSON* root = cJSON_CreateArray();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON array");
        nvs_close(handle);
        return false;
    }
    
    for (const auto& alarm : alarms_) {
        cJSON* item = cJSON_Parse(alarm.ToJson().c_str());
        if (item) {
            cJSON_AddItemToArray(root, item);
        } else {
            ESP_LOGW(TAG, "Failed to parse alarm to JSON, skipping");
        }
    }
    
    char* buf = cJSON_PrintUnformatted(root);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(root);
        nvs_close(handle);
        return false;
    }
    
    err = nvs_set_str(handle, "list", buf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set_str failed: %s", esp_err_to_name(err));
        cJSON_free(buf);
        cJSON_Delete(root);
        nvs_close(handle);
        return false;
    }
    cJSON_free(buf);
    cJSON_Delete(root);
    
    err = nvs_set_i32(handle, "next_id", next_id_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set_i32 failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Saved %d alarms to NVS successfully", alarms_.size());
    return true;
}

void AlarmManager::SetAlarmCallback(std::function<void(const Alarm&)> callback) {
    on_alarm_triggered_ = callback;
}

void AlarmManager::TriggerAlarm(const Alarm& alarm) {
    if (on_alarm_triggered_) {
        on_alarm_triggered_(alarm);
    }
}

void AlarmManager::CheckAndTriggerAlarms(int hour, int minute, int day_of_week) {
    std::lock_guard<std::recursive_mutex> lock(g_alarm_mutex);
    
    for (auto it = alarms_.begin(); it != alarms_.end(); ) {
        const auto& alarm = *it;
        if (alarm.enabled && alarm.hour == hour && alarm.minute == minute) {
            bool should_trigger = true;
            
            if (alarm.repeat > 0) {
                uint8_t repeat_mask = static_cast<uint8_t>(alarm.repeat);
                uint8_t day_bit = 1 << day_of_week;
                should_trigger = (repeat_mask & day_bit) != 0;
            }
            
            if (should_trigger) {
                ESP_LOGI(TAG, "Alarm triggered: %d %02d:%02d", alarm.id, alarm.hour, alarm.minute);
                
                TriggerAlarm(alarm);
                
                if (alarm.repeat == 0) {
                    it = alarms_.erase(it);
                    SaveAlarms();
                    ESP_LOGI(TAG, "One-time alarm %d triggered and auto-deleted", alarm.id);
                    continue;
                }
            }
        }
        ++it;
    }
}
