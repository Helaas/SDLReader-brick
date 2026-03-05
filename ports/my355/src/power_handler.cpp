#include "power_handler.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include "debug_log.h"
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
                DEBUG_LOG("Power handler: Found KEY_POWER capability on " << path);
                return fd;
            }
        }

        close(fd);
    }

    DEBUG_ERR("Power handler: No device with KEY_POWER capability found");
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
        DEBUG_ERR("Failed to find power input device");
        return false;
    }

    DEBUG_LOG("Power handler started (dynamic device discovery)");
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

    DEBUG_LOG("Power handler thread started");

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
                DEBUG_ERR("Device read error: " << strerror(errno));
                if (!reopenDevice())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                }
            }
        }
        else
        {
            // EOF - device disconnected
            DEBUG_LOG("Device disconnected, reopening...");
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
        DEBUG_LOG("PowerHandler: Long press detected while holding (" << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms) - shutting down");
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
            DEBUG_LOG("Waking from fake sleep mode");
            exitFakeSleep();
            m_powerButtonDown = false;
            m_longPressHandled = false;
            m_powerPressTime = std::chrono::steady_clock::time_point{}; // Don't register this as a new press
        }
        else
        {
            // Normal press
            DEBUG_LOG("Power button pressed");
            m_powerButtonDown = true;
            m_longPressHandled = false;
            m_powerPressTime = now;
        }
    }
    else if (ev.value == 0 && m_powerPressTime != std::chrono::steady_clock::time_point{})
    {
        // Button released after a valid press
        auto duration = now - m_powerPressTime;

        DEBUG_LOG("PowerHandler: Power button released after " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms");

        if (!m_longPressHandled && duration < SHORT_PRESS_MAX)
        {
            // Short press - try to sleep
            DEBUG_LOG("PowerHandler: Short press detected - calling attemptSleep()");
            attemptSleep();
        }
        else
        {
            DEBUG_LOG("PowerHandler: Long press detected (duration >= " << SHORT_PRESS_MAX.count() << "ms)");
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
    DEBUG_LOG("PowerHandler: Attempting sleep...");

    // First, check if any UI windows are open and close them
    if (m_preSleepCallback)
    {
        bool uiWasClosed = m_preSleepCallback();
        if (uiWasClosed)
        {
            DEBUG_LOG("PowerHandler: UI windows were closed, entering fake sleep and attempting real sleep");
        }
    }

    bool sleepSuccess = requestSleep();
    DEBUG_LOG("PowerHandler: requestSleep() returned: " << (sleepSuccess ? "true" : "false"));

    if (sleepSuccess)
    {
        // Real sleep succeeded
        DEBUG_LOG("PowerHandler: Real sleep successful");
        m_resume_ignore_until = std::chrono::steady_clock::now() + POST_RESUME_IGNORE_DURATION;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flushEvents();
    }
    else
    {
        // Real sleep failed - enter fake sleep mode
        DEBUG_LOG("PowerHandler: Real sleep failed - entering fake sleep mode");
        enterFakeSleep();
    }
}

void PowerHandler::enterFakeSleep()
{
    DEBUG_LOG("PowerHandler: Entering fake sleep mode...");
    m_in_fake_sleep.store(true);
    m_fake_sleep_start_time = std::chrono::steady_clock::now();

    DEBUG_LOG("PowerHandler: Calling sleep mode callback with true...");
    if (m_sleepModeCallback)
    {
        m_sleepModeCallback(true);
        DEBUG_LOG("PowerHandler: Sleep mode callback executed successfully");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    else
    {
        DEBUG_LOG("PowerHandler: ERROR - No sleep mode callback registered!");
    }

    DEBUG_LOG("PowerHandler: Entered fake sleep mode - screen should be off, inputs disabled");
}

void PowerHandler::exitFakeSleep()
{
    DEBUG_LOG("PowerHandler: Exiting fake sleep mode...");
    m_in_fake_sleep.store(false);

    DEBUG_LOG("PowerHandler: Calling sleep mode callback with false...");
    if (m_sleepModeCallback)
    {
        m_sleepModeCallback(false);
        DEBUG_LOG("PowerHandler: Sleep mode callback executed successfully");
    }
    else
    {
        DEBUG_LOG("PowerHandler: ERROR - No sleep mode callback registered!");
    }

    flushEvents();
    DEBUG_LOG("PowerHandler: Exited fake sleep mode - screen should be restored, inputs enabled");
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

        DEBUG_LOG("Attempting deep sleep from fake sleep mode...");
        if (requestSleep())
        {
            DEBUG_LOG("Deep sleep successful - exiting fake sleep mode");
            error_shown = false;
            exitFakeSleep();
        }
        else
        {
            auto time_in_fake_sleep = now - m_fake_sleep_start_time;
            if (time_in_fake_sleep >= std::chrono::seconds(30) && !error_shown)
            {
                DEBUG_LOG("Deep sleep has failed for 30+ seconds, showing error to user");
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
        DEBUG_LOG("Flushed " << flush_count << " stale input events");
    }
}

bool PowerHandler::requestSleep()
{
    DEBUG_LOG("Attempting to suspend device...");

    // MY355: Direct sysfs suspend (no platform scripts)
    // Method 1: mem suspend
    if (access("/sys/power/state", W_OK) == 0)
    {
        DEBUG_LOG("Using direct system suspend (mem)");
        int result = system("echo mem > /sys/power/state 2>/dev/null");
        if (result == 0)
        {
            DEBUG_LOG("Suspend successful");
            return true;
        }
        else
        {
            DEBUG_LOG("Direct system suspend failed with result: " << result);
        }
    }
    else
    {
        DEBUG_LOG("Direct system suspend not available (/sys/power/state not writable)");
    }

    // Method 2: freeze mode as fallback
    if (access("/sys/power/state", W_OK) == 0)
    {
        DEBUG_LOG("Trying freeze mode suspend");
        int result = system("echo freeze > /sys/power/state 2>/dev/null");
        if (result == 0)
        {
            DEBUG_LOG("Freeze suspend successful");
            return true;
        }
        else
        {
            DEBUG_LOG("Freeze mode suspend failed with result: " << result);
        }
    }

    DEBUG_LOG("Warning: No working suspend method found - will use fake sleep mode");
    DEBUG_ERR("INFO: Could not suspend device - falling back to fake sleep mode");

    return false;
}

void PowerHandler::requestShutdown()
{
    DEBUG_LOG("Attempting to shutdown device...");

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

    DEBUG_LOG("Exiting application...");
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
        DEBUG_ERR("Failed to reopen power input device");
        return false;
    }

    DEBUG_LOG("Power handler device reopened successfully (dynamic discovery)");
    flushEvents();

    return true;
}
