#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

struct input_event;

class PowerHandler
{
public:
    using ErrorCallback = std::function<void(const std::string&)>;
    using SleepModeCallback = std::function<void(bool)>; // true = enter fake sleep, false = exit fake sleep
    using PreSleepCallback = std::function<bool()>;      // Returns true if UI was closed, false if nothing was open

    PowerHandler();
    ~PowerHandler();

    // Start the power button monitoring thread
    bool start();

    // Stop the power button monitoring thread
    void stop();

    // Set callback for displaying error messages on GUI
    void setErrorCallback(ErrorCallback callback);

    // Set callback for entering/exiting fake sleep mode (black screen, disabled inputs)
    void setSleepModeCallback(SleepModeCallback callback);

    // Set callback to close UI windows before sleep (e.g., settings menu, number pad)
    void setPreSleepCallback(PreSleepCallback callback);

private:
    void threadMain();
    void handlePowerButtonEvent(const input_event& ev, std::chrono::steady_clock::time_point& press_time);
    void attemptSleep();
    void enterFakeSleep();
    void exitFakeSleep();
    void tryDeepSleep();
    bool requestSleep();
    void requestShutdown();
    bool reopenDevice();
    void flushEvents();

    static constexpr int POWER_KEY_CODE = 116;
    static constexpr const char* DEVICE_PATH = "/dev/input/event1";
    static constexpr const char* PLATFORM_SUSPEND_PATH_PRIMARY = "/mnt/SDCARD/SYSTEM/bin/suspend";
    static constexpr const char* PLATFORM_SUSPEND_PATH_SECONDARY = "/mnt/SDCARD/System/bin/suspend";
    static constexpr auto SHORT_PRESS_MAX = std::chrono::milliseconds(2000);
    static constexpr auto POST_RESUME_IGNORE_DURATION = std::chrono::milliseconds(500);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_in_fake_sleep{false};
    int m_device_fd{-1};
    std::chrono::steady_clock::time_point m_fake_sleep_start_time;
    std::chrono::steady_clock::time_point m_resume_ignore_until;
    ErrorCallback m_errorCallback;
    SleepModeCallback m_sleepModeCallback;
    PreSleepCallback m_preSleepCallback;
};
