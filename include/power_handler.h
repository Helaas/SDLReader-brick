#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

class PowerHandler
{
public:
    using ErrorCallback = std::function<void(const std::string&)>;
    
    PowerHandler();
    ~PowerHandler();

    // Start the power button monitoring thread
    bool start();
    
    // Stop the power button monitoring thread  
    void stop();
    
    // Set callback for displaying error messages on GUI
    void setErrorCallback(ErrorCallback callback);

private:
    void threadMain();
    bool requestSleep();
    void requestShutdown();
    bool reopenDevice();
    
    static constexpr int POWER_KEY_CODE = 116;
    static constexpr const char* DEVICE_PATH = "/dev/input/event1";
    static constexpr auto SHORT_PRESS_MAX = std::chrono::milliseconds(2000);
    static constexpr auto COOLDOWN_TIME = std::chrono::milliseconds(1000);
    
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_just_woke_up{false};
    int m_device_fd{-1};
    ErrorCallback m_errorCallback;
};
