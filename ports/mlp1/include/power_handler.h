#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

struct input_event;

class PowerHandler
{
public:
    using ErrorCallback = std::function<void(const std::string&)>;
    using SleepModeCallback = std::function<void(bool)>;
    using PreSleepCallback = std::function<bool()>;

    PowerHandler();
    ~PowerHandler();

    bool start();
    void stop();

    void setErrorCallback(ErrorCallback callback);
    void setSleepModeCallback(SleepModeCallback callback);
    void setPreSleepCallback(PreSleepCallback callback);

private:
    void threadMain();
    void handlePowerButtonEvent(const input_event& ev);
    void checkLongPressWhileHeld();
    void attemptStandby();
    void requestShutdown();
    bool reopenDevice();
    void flushEvents();

    int findPowerDevice();

    // Suspend via loong SDK or fallback
    bool sleepViaLoongSdk();
    bool sleepViaSysfs();

    static constexpr int POWER_KEY_CODE = 116;
    static constexpr auto LONG_PRESS_DURATION = std::chrono::milliseconds(3000);
    static constexpr auto POST_RESUME_IGNORE_DURATION = std::chrono::milliseconds(500);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    int m_device_fd{-1};
    bool m_powerButtonDown{false};
    bool m_longPressHandled{false};
    std::chrono::steady_clock::time_point m_powerPressTime;
    std::chrono::steady_clock::time_point m_resume_ignore_until;
    ErrorCallback m_errorCallback;
    SleepModeCallback m_sleepModeCallback;
    PreSleepCallback m_preSleepCallback;
};
