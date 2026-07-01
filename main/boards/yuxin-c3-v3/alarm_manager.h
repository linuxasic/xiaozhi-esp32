#pragma once

#include <vector>
#include <string>
#include <functional>
#include <mutex>

struct Alarm {
    int id;
    int hour;
    int minute;
    bool enabled;
    std::string message;
    int repeat;

    std::string ToJson() const;
    static Alarm FromJson(const std::string& json);
};

class AlarmManager {
public:
    AlarmManager();
    ~AlarmManager();

    bool AddAlarm(int hour, int minute, const std::string& message = "", int repeat = 0);
    bool RemoveAlarm(int id);
    bool EnableAlarm(int id, bool enabled);
    std::vector<Alarm> GetAlarms() const;
    Alarm* GetAlarm(int id);
    int GetNextAlarmId();
    void LoadAlarms();
    bool SaveAlarms();  // Changed return type from void to bool

    void SetAlarmCallback(std::function<void(const Alarm&)> callback);
    void TriggerAlarm(const Alarm& alarm);

private:
    std::vector<Alarm> alarms_;
    int next_id_;
    std::function<void(const Alarm&)> on_alarm_triggered_;
};

// Global recursive mutex for thread safety
extern std::recursive_mutex g_alarm_mutex;
