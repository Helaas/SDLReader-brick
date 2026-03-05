#include "power_handler.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

PowerHandler::PowerHandler() : m_running(false), m_in_fake_sleep(false), m_device_fd(-1)
{
}

PowerHandler::~PowerHandler()
{
    stop();
}

int PowerHandler::findPowerDevice()
{
    // Dynamically scan /dev/input/event* devices to find the one with KEY_POWER capability.
    // This is necessary on MY355 where the power button event device path is not fixed.
    unsigned char key_bits[(KEY_MAX + 1) / 8];

    for (int i = 0; i < 16; i++)
    {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        memset(key_bits, 0, sizeof(key_bits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0)
        {
            if (key_bits[KEY_POWER / 8] & (1 << (KEY_POWER % 8)))
            {
                std::cout << "Power handler: Found KEY_POWER capability on " << path << std::endl;
                return fd;
            }
        }

        close(fd);
    }

    std::cerr << "Power handler: No device with KEY_POWER capability found" << std::endl;
    return -1;
}

bool PowerHandler::start()
{
    if (m_running.load())
    {
        return true;
    }

    m_device_fd = findPowerDevice();
    if (m_device_fd < 0)
    {
        std::cerr << "Failed to find power input device" << std::endl;
        return false;
    }

    std::cout << "Power handler started (dynamic device discovery)" << std::endl;
    flushEvents();

    m_running.store(true);
    m_thread = std::thread(&PowerHandler::threadMain, this);

    return true;
}

void PowerHandler::stop()
{
    if (!m_running.load())
    {
        return;
    }

    m_running.store(false);
    m_in_fake_sleep.store(false);

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    if (m_device_fd >= 0)
    {
        close(m_device_fd);
        m_device_fd = -1;
    }
}

void PowerHandler::setErrorCallback(ErrorCallback callback)
{
    m_errorCallback = callback;
}

void PowerHandler::setSleepModeCallback(SleepModeCallback callback)
{
    m_sleepModeCallback = callback;
}

void PowerHandler::setPreSleepCallback(PreSleepCallback callback)
{
    m_preSleepCallback = callback;
}

void PowerHandler::threadMain()
{
    struct input_event ev;
    m_powerButtonDown = false;
    m_longPressHandled = false;
    m_powerPressTime = std::chrono::steady_clock::time_point{};

    std::cout << "Power handler thread started" << std::endl;

    while (m_running.load())
    {
        checkLongPressWhileHeld();
        ssize_t bytes_read = read(m_device_fd, &ev, sizeof(ev));

        if (bytes_read == sizeof(ev))
        {
            // Only process power button events
            // Accept both KEY_POWER and code 102 for compatibility
            if (ev.type == EV_KEY && (ev.code == POWER_KEY_CODE || ev.code == 102))
            {
                handlePowerButtonEvent(ev);
                checkLongPressWhileHeld();
            }
        }
        else if (bytes_read < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                // If we're in fake sleep mode, keep trying to achieve real sleep
                if (m_in_fake_sleep.load())
                {
                    tryDeepSleep();
                }
                checkLongPressWhileHeld();
            }
            else
            {
                std::cerr << "Device read error: " << strerror(errno) << std::endl;
                if (!reopenDevice())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
        }
        else
        {
            // EOF - device disconnected
            std::cout << "Device disconnected, reopening..." << std::endl;
            if (!reopenDevice())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
    }
}

void PowerHandler::checkLongPressWhileHeld()
{
    if (!m_powerButtonDown || m_longPressHandled || m_powerPressTime == std::chrono::steady_clock::time_point{})
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto duration = now - m_powerPressTime;
    if (duration >= SHORT_PRESS_MAX)
    {
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        std::cout << "PowerHandler: Long press detected while holding (" << duration_ms << "ms) - shutting down" << std::endl;
        m_longPressHandled = true;
        m_powerButtonDown = false;
        requestShutdown();
    }
}

void PowerHandler::handlePowerButtonEvent(const input_event& ev)
{
    auto now = std::chrono::steady_clock::now();
    if (m_resume_ignore_until != std::chrono::steady_clock::time_point{} &&
        now < m_resume_ignore_until)
    {
        // Ignore any button activity immediately after resuming from a real sleep.
        if (ev.value == 0)
        {
            m_powerButtonDown = false;
            m_longPressHandled = false;
            m_powerPressTime = std::chrono::steady_clock::time_point{};
        }
        return;
    }
    else if (m_resume_ignore_until != std::chrono::steady_clock::time_point{} && now >= m_resume_ignore_until)
    {
        m_resume_ignore_until = std::chrono::steady_clock::time_point{};
    }

    if (ev.value == 1)
    {
        // Button pressed
        if (m_in_fake_sleep.load())
        {
            // Wake from fake sleep
            std::cout << "Waking from fake sleep mode" << std::endl;
            exitFakeSleep();
            m_powerButtonDown = false;
            m_longPressHandled = false;
            m_powerPressTime = std::chrono::steady_clock::time_point{}; // Don't register this as a new press
        }
        else
        {
            // Normal press
            std::cout << "Power button pressed" << std::endl;
            m_powerButtonDown = true;
            m_longPressHandled = false;
            m_powerPressTime = now;
        }
    }
    else if (ev.value == 0 && m_powerPressTime != std::chrono::steady_clock::time_point{})
    {
        // Button released after a valid press
        auto duration = now - m_powerPressTime;
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

        std::cout << "PowerHandler: Power button released after " << duration_ms << "ms" << std::endl;

        if (!m_longPressHandled && duration < SHORT_PRESS_MAX)
        {
            // Short press - try to sleep
            std::cout << "PowerHandler: Short press detected - calling attemptSleep()" << std::endl;
            attemptSleep();
        }
        else
        {
            std::cout << "PowerHandler: Long press detected (duration >= " << SHORT_PRESS_MAX.count() << "ms)" << std::endl;
        }

        m_powerButtonDown = false;
        m_longPressHandled = false;
        m_powerPressTime = std::chrono::steady_clock::time_point{};
    }
    else if (ev.value == 2)
    {
        // Repeat events are optional on some kernels; keep support but don't rely on it.
        checkLongPressWhileHeld();
    }
}

void PowerHandler::attemptSleep()
{
    std::cout << "PowerHandler: Attempting sleep..." << std::endl;

    // First, check if any UI windows are open and close them
    if (m_preSleepCallback)
    {
        bool uiWasClosed = m_preSleepCallback();
        if (uiWasClosed)
        {
            std::cout << "PowerHandler: UI windows were closed, entering fake sleep and attempting real sleep" << std::endl;
        }
    }

    bool sleepSuccess = requestSleep();
    std::cout << "PowerHandler: requestSleep() returned: " << (sleepSuccess ? "true" : "false") << std::endl;

    if (sleepSuccess)
    {
        // Real sleep succeeded
        std::cout << "PowerHandler: Real sleep successful" << std::endl;
        m_resume_ignore_until = std::chrono::steady_clock::now() + POST_RESUME_IGNORE_DURATION;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flushEvents();
    }
    else
    {
        // Real sleep failed - enter fake sleep mode
        std::cout << "PowerHandler: Real sleep failed - entering fake sleep mode" << std::endl;
        enterFakeSleep();
    }
}

void PowerHandler::enterFakeSleep()
{
    std::cout << "PowerHandler: Entering fake sleep mode..." << std::endl;
    m_in_fake_sleep.store(true);
    m_fake_sleep_start_time = std::chrono::steady_clock::now();

    std::cout << "PowerHandler: Calling sleep mode callback with true..." << std::endl;
    if (m_sleepModeCallback)
    {
        m_sleepModeCallback(true);
        std::cout << "PowerHandler: Sleep mode callback executed successfully" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    else
    {
        std::cout << "PowerHandler: ERROR - No sleep mode callback registered!" << std::endl;
    }

    std::cout << "PowerHandler: Entered fake sleep mode - screen should be off, inputs disabled" << std::endl;
}

void PowerHandler::exitFakeSleep()
{
    std::cout << "PowerHandler: Exiting fake sleep mode..." << std::endl;
    m_in_fake_sleep.store(false);

    std::cout << "PowerHandler: Calling sleep mode callback with false..." << std::endl;
    if (m_sleepModeCallback)
    {
        m_sleepModeCallback(false);
        std::cout << "PowerHandler: Sleep mode callback executed successfully" << std::endl;
    }
    else
    {
        std::cout << "PowerHandler: ERROR - No sleep mode callback registered!" << std::endl;
    }

    flushEvents();
    std::cout << "PowerHandler: Exited fake sleep mode - screen should be restored, inputs enabled" << std::endl;
}

void PowerHandler::tryDeepSleep()
{
    static auto last_attempt = std::chrono::steady_clock::time_point{};
    static bool error_shown = false;
    auto now = std::chrono::steady_clock::now();

    // Try deep sleep every 2 seconds while in fake sleep
    if ((now - last_attempt) >= std::chrono::seconds(2))
    {
        last_attempt = now;

        std::cout << "Attempting deep sleep from fake sleep mode..." << std::endl;
        if (requestSleep())
        {
            std::cout << "Deep sleep successful - exiting fake sleep mode" << std::endl;
            error_shown = false;
            exitFakeSleep();
        }
        else
        {
            auto time_in_fake_sleep = now - m_fake_sleep_start_time;
            if (time_in_fake_sleep >= std::chrono::seconds(30) && !error_shown)
            {
                std::cout << "Deep sleep has failed for 30+ seconds, showing error to user" << std::endl;
                if (m_errorCallback)
                {
                    m_errorCallback("Suspend failed. Please try again in a few seconds.");
                }
                error_shown = true;
            }
        }
    }
}

void PowerHandler::flushEvents()
{
    if (m_device_fd < 0)
        return;

    struct input_event ev;
    int flush_count = 0;
    while (read(m_device_fd, &ev, sizeof(ev)) == sizeof(ev))
    {
        flush_count++;
        if (flush_count > 100)
            break; // Safety limit
    }

    if (flush_count > 0)
    {
        std::cout << "Flushed " << flush_count << " stale input events" << std::endl;
    }
}

bool PowerHandler::requestSleep()
{
    std::cout << "Attempting to suspend device..." << std::endl;

    // MY355: Direct sysfs suspend (no platform scripts)
    // Method 1: mem suspend
    if (access("/sys/power/state", W_OK) == 0)
    {
        std::cout << "Using direct system suspend (mem)" << std::endl;
        int result = system("echo mem > /sys/power/state 2>/dev/null");
        if (result == 0)
        {
            std::cout << "Suspend successful" << std::endl;
            return true;
        }
        else
        {
            std::cout << "Direct system suspend failed with result: " << result << std::endl;
        }
    }
    else
    {
        std::cout << "Direct system suspend not available (/sys/power/state not writable)" << std::endl;
    }

    // Method 2: freeze mode as fallback
    if (access("/sys/power/state", W_OK) == 0)
    {
        std::cout << "Trying freeze mode suspend" << std::endl;
        int result = system("echo freeze > /sys/power/state 2>/dev/null");
        if (result == 0)
        {
            std::cout << "Freeze suspend successful" << std::endl;
            return true;
        }
        else
        {
            std::cout << "Freeze mode suspend failed with result: " << result << std::endl;
        }
    }

    std::cout << "Warning: No working suspend method found - will use fake sleep mode" << std::endl;
    std::cerr << "INFO: Could not suspend device - falling back to fake sleep mode" << std::endl;

    return false;
}

void PowerHandler::requestShutdown()
{
    std::cout << "Attempting to shutdown device..." << std::endl;

    if (m_errorCallback)
    {
        m_errorCallback("Shutting down...");
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    system("rm -f /tmp/nextui_exec && sync");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    system("touch /tmp/poweroff");
    sync();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Exiting application..." << std::endl;
    std::exit(0);
}

bool PowerHandler::reopenDevice()
{
    if (m_device_fd >= 0)
    {
        close(m_device_fd);
        m_device_fd = -1;
    }

    // Use dynamic discovery to find the power device again
    m_device_fd = findPowerDevice();
    if (m_device_fd < 0)
    {
        std::cerr << "Failed to reopen power input device" << std::endl;
        return false;
    }

    std::cout << "Power handler device reopened successfully (dynamic discovery)" << std::endl;
    flushEvents();

    return true;
}
