#pragma once

#include <atomic>
#include <thread>
#include <chrono>

class PowerHandler
{
public:
    PowerHandler();
    ~PowerHandler();

    // Start the power button monitoring thread
    bool start();
    
    // Stop the power button monitoring thread  
    void stop();

private:
    void threadMain();
    void requestSleep();
    void requestShutdown();
    bool reopenDevice();
    
    static constexpr int POWER_KEY_CODE = 116;
    static constexpr const char* DEVICE_PATH = "/dev/input/event1";
    static constexpr auto SHORT_PRESS_MAX = std::chrono::milliseconds(2000);
    static constexpr auto COOLDOWN_TIME = std::chrono::milliseconds(1000);
    // Wake Event → [WAKE_GRACE_PERIOD] → [POST_GRACE_DELAY]   → Normal Operation
    //             ↑                       ↑                   ↑
    //             Hardware stabilization  User stabilization  Button responsive
    static constexpr auto WAKE_GRACE_PERIOD = std::chrono::milliseconds(1500);     // 1.5 seconds
    static constexpr auto POST_GRACE_DELAY = std::chrono::milliseconds(500);      // .5 seconds
    
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_just_woke_up{false};
    int m_device_fd{-1};
};
