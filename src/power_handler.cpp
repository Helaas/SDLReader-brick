#include "power_handler.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <thread>

PowerHandler::PowerHandler()
{
}

PowerHandler::~PowerHandler()
{
    stop();
}

bool PowerHandler::start()
{
    if (m_running.load()) {
        return true; // Already running
    }
    
    // Open the device in non-blocking mode to avoid interfering with SDL
    m_device_fd = open(DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (m_device_fd < 0) {
        std::cerr << "Failed to open input device: " << DEVICE_PATH << std::endl;
        return false;
    }
    
    std::cout << "Power handler listening on device: " << DEVICE_PATH << std::endl;
    
    // Flush any initial stale events more thoroughly
    struct input_event ev;
    int flush_count = 0;
    
    // First, read all available events
    while (read(m_device_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        flush_count++;
        if (flush_count > 1000) break; // Safety limit
    }
    
    // Wait a bit for any lingering events
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Flush again
    while (read(m_device_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        flush_count++;
        if (flush_count > 1000) break; // Safety limit
    }
    
    if (flush_count > 0) {
        std::cout << "Flushed " << flush_count << " initial stale input events" << std::endl;
    }
    
    m_running.store(true);
    m_thread = std::thread(&PowerHandler::threadMain, this);
    
    return true;
}

void PowerHandler::stop()
{
    if (!m_running.load()) {
        return;
    }
    
    m_running.store(false);
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    if (m_device_fd >= 0) {
        close(m_device_fd);
        m_device_fd = -1;
    }
}

void PowerHandler::setErrorCallback(ErrorCallback callback)
{
    m_errorCallback = callback;
}

void PowerHandler::threadMain()
{
    struct input_event ev;
    auto press_time = std::chrono::steady_clock::time_point{};
    auto cooldown_until = std::chrono::steady_clock::time_point{};
    auto last_success = std::chrono::steady_clock::now();
    auto wake_grace_until = std::chrono::steady_clock::time_point{}; // No initial grace period
    const auto DEVICE_TIMEOUT = std::chrono::seconds(5); // If no successful reads for 30s, try to reopen
    const auto WAKE_GRACE_PERIOD = std::chrono::milliseconds(3000); // Extended to 3s after wake for better stability
    auto last_button_event = std::chrono::steady_clock::time_point{}; // Track last button event for debouncing
    const auto DEBOUNCE_TIME = std::chrono::milliseconds(200); // Minimum time between processing button events
    bool grace_period_just_ended = false; // Flag to track when grace period just ended
    auto grace_end_time = std::chrono::steady_clock::time_point{}; // When grace period ended
    const auto POST_GRACE_DELAY = std::chrono::milliseconds(1000); // Extra delay after grace period ends - increased
    bool came_from_wake = false; // Track if current grace period is from a wake event
    
    std::cout << "Power handler thread started - ready for power button events" << std::endl;
    
    // Status reporting for grace periods
    auto last_status_report = std::chrono::steady_clock::now();
    const auto STATUS_REPORT_INTERVAL = std::chrono::milliseconds(1000); // Report status every second
    
    while (m_running.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Periodic status reporting (independent of input events)
        if ((now - last_status_report) >= STATUS_REPORT_INTERVAL) {
            if (now < wake_grace_until) {
                auto grace_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(wake_grace_until - now).count();
                std::cout << "Grace period active: " << grace_remaining << "ms remaining" << std::endl;
            } else if (grace_period_just_ended && came_from_wake && (now - grace_end_time) < POST_GRACE_DELAY) {
                auto delay_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(POST_GRACE_DELAY - (now - grace_end_time)).count();
                std::cout << "Post-grace delay active: " << delay_remaining << "ms remaining" << std::endl;
            }
            last_status_report = now;
        }
        
        // Check if grace period just ended and set up post-grace delay (only for wake events)
        if (!grace_period_just_ended && wake_grace_until != std::chrono::steady_clock::time_point{} && now >= wake_grace_until) {
            grace_period_just_ended = true;
            grace_end_time = now;
            if (came_from_wake) {
                std::cout << "Wake grace period ended, starting " << std::chrono::duration_cast<std::chrono::milliseconds>(POST_GRACE_DELAY).count() << "ms post-grace delay" << std::endl;
            } else {
                std::cout << "Grace period ended" << std::endl;
            }
        }
        
        ssize_t bytes_read = read(m_device_fd, &ev, sizeof(ev));
        if (bytes_read == sizeof(ev)) {
            last_success = now; // Update last successful read time
            
            // Check if we just woke up from suspend and need to set grace period
            if (m_just_woke_up.load()) {
                std::cout << "Wake detected - setting extended grace period and aggressively resetting state" << std::endl;
                
                // Flush any stale events that might have accumulated during suspend - be more aggressive
                struct input_event flush_ev;
                int flush_count = 0;
                while (read(m_device_fd, &flush_ev, sizeof(flush_ev)) == sizeof(flush_ev)) {
                    flush_count++;
                    if (flush_count > 200) break; // Increased safety limit
                }
                
                // Wait a bit more and flush again to catch any lingering events
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                while (read(m_device_fd, &flush_ev, sizeof(flush_ev)) == sizeof(flush_ev)) {
                    flush_count++;
                    if (flush_count > 300) break; // Safety limit
                }
                
                if (flush_count > 0) {
                    std::cout << "Flushed " << flush_count << " stale events after wake" << std::endl;
                }
                
                wake_grace_until = now + WAKE_GRACE_PERIOD;
                came_from_wake = true; // Mark this grace period as coming from wake
                press_time = std::chrono::steady_clock::time_point{}; // Critical: reset any pending press
                cooldown_until = std::chrono::steady_clock::time_point{}; // Reset cooldown
                last_button_event = std::chrono::steady_clock::time_point{}; // Reset debounce
                grace_period_just_ended = false; // Reset post-grace delay flag
                grace_end_time = std::chrono::steady_clock::time_point{};
                m_just_woke_up.store(false);
            }
            
            // Check cooldown period first
            if (now < cooldown_until) {
                auto cooldown_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(cooldown_until - now).count();
                std::cout << "Event ignored - cooldown active (" << cooldown_remaining << "ms remaining)" << std::endl;
                
                // Show user message for cooldown
                if (m_errorCallback && ev.type == EV_KEY && ev.code == POWER_KEY_CODE && ev.value == 0) {
                    m_errorCallback("Power button cooling down. Please wait a moment.");
                }
                continue;
            }
            
            // Check wake grace period (ignore events shortly after wake/reopen)
            if (now < wake_grace_until) {
                auto grace_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(wake_grace_until - now).count();
                if (ev.type == EV_KEY && ev.code == POWER_KEY_CODE) {
                    std::cout << "Ignoring power button event during startup/wake grace period (" << grace_remaining << "ms remaining)" << std::endl;
                    
                    // Show user message for grace period
                    if (m_errorCallback && ev.value == 0) { // Only on button release to avoid spam
                        m_errorCallback("System just woke up. Please wait a moment.");
                    }
                }
                grace_period_just_ended = false; // Reset flag while in grace period
                continue;
            }
            
            // Check post-grace delay (extra protection against immediate button presses after wake)
            if (grace_period_just_ended && came_from_wake && (now - grace_end_time) < POST_GRACE_DELAY) {
                auto delay_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(POST_GRACE_DELAY - (now - grace_end_time)).count();
                if (ev.type == EV_KEY && ev.code == POWER_KEY_CODE) {
                    std::cout << "Ignoring power button event during post-wake delay (" << delay_remaining << "ms remaining)" << std::endl;
                    
                    // Show user message for post-wake delay
                    if (m_errorCallback && ev.value == 0) { // Only on button release to avoid spam
                        m_errorCallback("System stabilizing after wake. Please wait.");
                    }
                }
                continue;
            }
            
            // Only process key events for the power button
            if (ev.type == EV_KEY && ev.code == POWER_KEY_CODE) {
                std::cout << "Power button event: value=" << ev.value << " press_time_set=" 
                         << (press_time != std::chrono::steady_clock::time_point{}) << std::endl;
                
                if (ev.value == 0 && press_time != std::chrono::steady_clock::time_point{}) {
                    // Key released
                    auto duration = now - press_time;
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
                    press_time = std::chrono::steady_clock::time_point{}; // Reset
                    
                    std::cout << "Button released after " << duration_ms << "ms" << std::endl;
                    
                    // Only proceed if this is a reasonable press duration (debounce very short presses)
                    if (duration >= std::chrono::milliseconds(50) && duration < SHORT_PRESS_MAX) {
                        std::cout << "Short press detected, suspending..." << std::endl;
                        requestSleep();
                        cooldown_until = now + COOLDOWN_TIME;
                        auto cooldown_end = std::chrono::duration_cast<std::chrono::seconds>(cooldown_until.time_since_epoch()).count();
                        std::cout << "Cooldown set until epoch " << cooldown_end << std::endl;
                    } else if (duration < std::chrono::milliseconds(50)) {
                        std::cout << "Button press too short (" << duration_ms << "ms), ignoring" << std::endl;
                    }
                } else if (ev.value == 1) {
                    // Key pressed - only register if we don't already have a press time
                    if (press_time == std::chrono::steady_clock::time_point{}) {
                        std::cout << "Button pressed down" << std::endl;
                        press_time = now;
                        last_button_event = now; // Update debounce timer only on new press
                    } else {
                        // Add debouncing for duplicate press events
                        if (last_button_event != std::chrono::steady_clock::time_point{} && 
                            (now - last_button_event) < DEBOUNCE_TIME) {
                            auto debounce_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(DEBOUNCE_TIME - (now - last_button_event)).count();
                            std::cout << "Debouncing duplicate press event (" << debounce_remaining << "ms remaining)" << std::endl;
                            
                            // Show user message for rapid button pressing (only occasionally to avoid spam)
                            if (m_errorCallback && debounce_remaining > 150) { // Only show if significant time left
                                m_errorCallback("Please don't press the button so rapidly.");
                            }
                        } else {
                            std::cout << "Ignoring duplicate button press event" << std::endl;
                        }
                    }
                } else if (ev.value == 2) {
                    // Key held down (repeat event) - apply debouncing to prevent spam
                    if (last_button_event != std::chrono::steady_clock::time_point{} && 
                        (now - last_button_event) < DEBOUNCE_TIME) {
                        auto debounce_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(DEBOUNCE_TIME - (now - last_button_event)).count();
                        std::cout << "Debouncing repeat event (" << debounce_remaining << "ms remaining)" << std::endl;
                        continue;
                    }
                    
                    last_button_event = now;
                    
                    if (press_time != std::chrono::steady_clock::time_point{}) {
                        auto duration = now - press_time;
                        if (duration >= SHORT_PRESS_MAX) {
                            std::cout << "Button held down for 2+ seconds, shutting down..." << std::endl;
                            requestShutdown();
                            cooldown_until = now + COOLDOWN_TIME;
                            press_time = std::chrono::steady_clock::time_point{}; // Reset to prevent multiple shutdowns
                        }
                    }
                }
            }
        } else if (bytes_read < 0) {
            // For non-blocking reads, EAGAIN/EWOULDBLOCK is normal
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, sleep briefly and continue
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            } else if (m_running.load()) {
                // Other error - device might be stale after suspend/resume
                std::cerr << "Failed to read input event: " << strerror(errno) << std::endl;
                
                // Try to reopen the device
                if (reopenDevice()) {
                    std::cout << "Device reopened successfully after error" << std::endl;
                    last_success = std::chrono::steady_clock::now();
                    wake_grace_until = last_success + WAKE_GRACE_PERIOD; // Set grace period
                    came_from_wake = true; // Mark as wake event
                    // Reset button state after device reopen
                    press_time = std::chrono::steady_clock::time_point{};
                    cooldown_until = std::chrono::steady_clock::time_point{};
                    last_button_event = std::chrono::steady_clock::time_point{};
                    grace_period_just_ended = false;
                    grace_end_time = std::chrono::steady_clock::time_point{};
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
        } else {
            // bytes_read == 0 means EOF or device disconnected
            std::cout << "Device disconnected or EOF, attempting to reopen..." << std::endl;
            if (reopenDevice()) {
                std::cout << "Device reopened successfully after EOF" << std::endl;
                last_success = std::chrono::steady_clock::now();
                wake_grace_until = last_success + WAKE_GRACE_PERIOD; // Set grace period
                came_from_wake = true; // Mark as wake event
                // Reset button state after device reopen
                press_time = std::chrono::steady_clock::time_point{};
                cooldown_until = std::chrono::steady_clock::time_point{};
                last_button_event = std::chrono::steady_clock::time_point{};
                grace_period_just_ended = false;
                grace_end_time = std::chrono::steady_clock::time_point{};
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
        
        // Check if device has been unresponsive for too long (after suspend/resume)
        if ((now - last_success) > DEVICE_TIMEOUT) {
            std::cout << "Device unresponsive for " << std::chrono::duration_cast<std::chrono::seconds>(now - last_success).count() 
                      << " seconds, attempting to reopen..." << std::endl;
            if (reopenDevice()) {
                std::cout << "Device reopened successfully after timeout" << std::endl;
                last_success = now;
                wake_grace_until = now + WAKE_GRACE_PERIOD; // Set grace period
                // Reset button state after device reopen
                press_time = std::chrono::steady_clock::time_point{};
                cooldown_until = std::chrono::steady_clock::time_point{};
                last_button_event = std::chrono::steady_clock::time_point{};
            }
        }
    }
}

bool PowerHandler::reopenDevice()
{
    // Close current file descriptor if open
    if (m_device_fd >= 0) {
        close(m_device_fd);
        m_device_fd = -1;
    }
    
    // Try to reopen the device
    m_device_fd = open(DEVICE_PATH, O_RDONLY | O_NONBLOCK);
    if (m_device_fd < 0) {
        std::cerr << "Failed to reopen input device: " << DEVICE_PATH << " - " << strerror(errno) << std::endl;
        return false;
    }
    
    std::cout << "Power handler device reopened successfully: " << DEVICE_PATH << std::endl;
    
    // Flush any stale events that might be queued (especially from suspend/wake cycle)
    struct input_event ev;
    int flush_count = 0;
    while (read(m_device_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        flush_count++;
        if (flush_count > 100) break; // Safety limit
    }
    
    if (flush_count > 0) {
        std::cout << "Flushed " << flush_count << " stale input events after device reopen" << std::endl;
    }
    
    return true;
}

bool PowerHandler::requestSleep()
{
    std::cout << "Attempting to suspend device..." << std::endl;
    
    // Method 1: Direct system suspend (NextUI primary method)
    if (access("/sys/power/state", W_OK) == 0) {
        std::cout << "Using direct system suspend" << std::endl;
        int result = system("echo mem > /sys/power/state 2>/dev/null");
        if (result == 0) {
            std::cout << "Suspend successful, setting wake flag" << std::endl;
            m_just_woke_up.store(true);
            return true;
        }
    }
    
    // Method 2: Platform suspend script (NextUI secondary method)
    if (access("/mnt/SDCARD/System/bin/suspend", X_OK) == 0) {
        std::cout << "Using platform suspend script" << std::endl;
        int result = system("/mnt/SDCARD/System/bin/suspend");
        if (result == 0) {
            std::cout << "Platform suspend successful, setting wake flag" << std::endl;
            m_just_woke_up.store(true);
            return true;
        }
    }
    
    // Method 3: Try freeze mode as fallback
    if (access("/sys/power/state", W_OK) == 0) {
        std::cout << "Trying freeze mode suspend" << std::endl;
        int result = system("echo freeze > /sys/power/state 2>/dev/null");
        if (result == 0) {
            std::cout << "Freeze suspend successful, setting wake flag" << std::endl;
            m_just_woke_up.store(true);
            return true;
        }
    }
    
    std::cout << "Warning: No working suspend method found" << std::endl;
    std::cerr << "ERROR: Could not suspend device - no working methods available" << std::endl;
    
    // Show error message on GUI if callback is available
    if (m_errorCallback) {
        m_errorCallback("Suspend failed. Please try again in a few seconds.");
    }
    
    return false;
}

void PowerHandler::requestShutdown()
{
    std::cout << "Attempting to shutdown device..." << std::endl;
    
    // Method 1: NextUI-style shutdown script
    if (access("/mnt/SDCARD/System/bin/shutdown", X_OK) == 0) {
        std::cout << "Using NextUI shutdown script" << std::endl;
        system("/mnt/SDCARD/System/bin/shutdown");
        return;
    }
    
    // Method 2: Standard poweroff command
    std::cout << "Using poweroff command" << std::endl;
    system("poweroff");
}
