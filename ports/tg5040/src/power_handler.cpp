#include "power_handler.h"
#include "platform_runtime.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
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
    unsigned char key_bits[(KEY_MAX + 1) / 8];

    for (int i = 0; i < 16; ++i)
    {
        char path[32];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        memset(key_bits, 0, sizeof(key_bits));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) >= 0 &&
            (key_bits[KEY_POWER / 8] & (1 << (KEY_POWER % 8))))
        {
            DEBUG_LOG("Power handler: Found KEY_POWER capability on " << path);
            return fd;
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
    auto press_time = std::chrono::steady_clock::time_point{};

    DEBUG_LOG("Power handler thread started");

    while (m_running.load())
    {
        // Some devices (including MY355) never emit key-repeat events.
        if (press_time != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - press_time >= SHORT_PRESS_MAX)
        {
            press_time = std::chrono::steady_clock::time_point{};
            requestShutdown();
        }

        ssize_t bytes_read = read(m_device_fd, &ev, sizeof(ev));

        if (bytes_read == sizeof(ev))
        {
            // Only process power button events
            if (ev.type == EV_KEY &&
                (ev.code == POWER_KEY_CODE || (isMy355Platform() && ev.code == 102)))
            {
                handlePowerButtonEvent(ev, press_time);
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

void PowerHandler::handlePowerButtonEvent(const input_event& ev, std::chrono::steady_clock::time_point& press_time)
{
    auto now = std::chrono::steady_clock::now();
    if (m_resume_ignore_until != std::chrono::steady_clock::time_point{} &&
        now < m_resume_ignore_until)
    {
        // Ignore any button activity immediately after resuming from a real sleep.
        if (ev.value == 0)
        {
            press_time = std::chrono::steady_clock::time_point{};
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
            press_time = std::chrono::steady_clock::time_point{}; // Don't register this as a new press
        }
        else
        {
            // Normal press
            DEBUG_LOG("Power button pressed");
            press_time = now;
        }
    }
    else if (ev.value == 0 && press_time != std::chrono::steady_clock::time_point{})
    {
        // Button released after a valid press
        auto duration = now - press_time;
        press_time = std::chrono::steady_clock::time_point{};

        DEBUG_LOG("PowerHandler: Power button released after " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms");

        if (duration < SHORT_PRESS_MAX)
        {
            // Short press - try to sleep
            DEBUG_LOG("PowerHandler: Short press detected - calling attemptSleep()");
            attemptSleep();
        }
        else
        {
            DEBUG_LOG("PowerHandler: Long press detected (duration >= " << SHORT_PRESS_MAX.count() << "ms)");
            requestShutdown();
        }
    }
    else if (ev.value == 2 && press_time != std::chrono::steady_clock::time_point{})
    {
        // Button held down
        auto duration = now - press_time;
        if (duration >= SHORT_PRESS_MAX)
        {
            DEBUG_LOG("Long press detected - shutting down");
            requestShutdown();
            press_time = std::chrono::steady_clock::time_point{};
        }
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
            // Continue to sleep logic below instead of returning
        }
    }

    bool sleepSuccess = requestSleep();
    DEBUG_LOG("PowerHandler: requestSleep() returned: " << (sleepSuccess ? "true" : "false"));

    if (sleepSuccess)
    {
        // Real sleep succeeded
        DEBUG_LOG("PowerHandler: Real sleep successful");
        // Ignore power button events briefly after resume so the wake button release
        // does not immediately trigger another suspend request.
        m_resume_ignore_until = std::chrono::steady_clock::now() + POST_RESUME_IGNORE_DURATION;

        // Give the input subsystem a moment to deliver the wake button release
        // signal before we flush the queue, otherwise the release event may land
        // after flushEvents() and be treated as a new short press.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flushEvents(); // Flush events after waking
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
    m_fake_sleep_start_time = std::chrono::steady_clock::now(); // Track when fake sleep started

    DEBUG_LOG("PowerHandler: Calling sleep mode callback with true...");
    if (m_sleepModeCallback)
    {
        m_sleepModeCallback(true); // Enable fake sleep (black screen, disable inputs)
        DEBUG_LOG("PowerHandler: Sleep mode callback executed successfully");

        // Give the main thread time to render the black screen
        // This helps ensure the screen actually goes black before we continue
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
        m_sleepModeCallback(false); // Disable fake sleep (restore screen, enable inputs)
        DEBUG_LOG("PowerHandler: Sleep mode callback executed successfully");
    }
    else
    {
        DEBUG_LOG("PowerHandler: ERROR - No sleep mode callback registered!");
    }

    flushEvents(); // Flush any accumulated events
    DEBUG_LOG("PowerHandler: Exited fake sleep mode - screen should be restored, inputs enabled");
}

void PowerHandler::tryDeepSleep()
{
    static auto last_attempt = std::chrono::steady_clock::time_point{};
    static bool error_shown = false; // Track if we've already shown the error
    auto now = std::chrono::steady_clock::now();

    // Try deep sleep every 2 seconds while in fake sleep
    if ((now - last_attempt) >= std::chrono::seconds(2))
    {
        last_attempt = now;

        DEBUG_LOG("Attempting deep sleep from fake sleep mode...");
        if (requestSleep())
        {
            // Deep sleep succeeded - exit fake sleep
            DEBUG_LOG("Deep sleep successful - exiting fake sleep mode");
            error_shown = false; // Reset error flag for next time
            exitFakeSleep();
        }
        else
        {
            // Check if we've been trying for more than 30 seconds
            auto time_in_fake_sleep = now - m_fake_sleep_start_time;
            if (time_in_fake_sleep >= std::chrono::seconds(30) && !error_shown)
            {
                DEBUG_LOG("Deep sleep has failed for 30+ seconds, showing error to user");
                if (m_errorCallback)
                {
                    m_errorCallback("Suspend failed. Please try again in a few seconds.");
                }
                error_shown = true; // Only show the error once per fake sleep session
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

    auto tryPlatformSuspend = [](const char* path) -> bool
    {
        if (!path)
        {
            return false;
        }

        if (access(path, X_OK) == 0)
        {
            DEBUG_LOG("Using platform suspend script: " << path);
            int result = system(path);
            if (result == 0)
            {
                DEBUG_LOG("Platform suspend successful");
                return true;
            }

            DEBUG_LOG("Platform suspend script failed with result: " << result);
            return false;
        }

        DEBUG_LOG("Platform suspend script not available (" << path << " not executable)");
        return false;
    };

    // Method 1: Platform suspend script (preferred to match NextUI behavior)
    bool platformSuspend = false;
    const char* systemPath = getenv("SYSTEM_PATH");
    if (systemPath && systemPath[0])
    {
        std::string suspendPath = std::string(systemPath) + "/bin/suspend";
        platformSuspend = tryPlatformSuspend(suspendPath.c_str());
    }
    if (platformSuspend || tryPlatformSuspend(PLATFORM_SUSPEND_PATH_PRIMARY) ||
        tryPlatformSuspend(PLATFORM_SUSPEND_PATH_SECONDARY))
    {
        return true;
    }

    // Method 2: Direct system suspend
    if (access("/sys/power/state", W_OK) == 0)
    {
        DEBUG_LOG("Using direct system suspend");
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

    // Method 3: Try freeze mode as fallback
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
    else
    {
        DEBUG_LOG("Freeze mode suspend not available (/sys/power/state not writable)");
    }

    DEBUG_LOG("Warning: No working suspend method found - will use fake sleep mode");
    DEBUG_ERR("INFO: Could not suspend device - falling back to fake sleep mode");

    return false;
}

void PowerHandler::requestShutdown()
{
    //  Manual shutdown sequence (NextUI-style)
    DEBUG_LOG("Attempting to shutdown device...");

    // Display shutdown message on GUI
    if (m_errorCallback)
    {
        m_errorCallback("Shutting down...");
    }

    // Give the GUI time to render the shutdown message
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Clean up temporary files and sync
    system("rm -f /tmp/nextui_exec && sync");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Signal poweroff
    system("touch /tmp/poweroff");
    sync();

    // Keep the message on screen for a moment
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Kill the application
    DEBUG_LOG("Exiting application...");
    std::exit(0);
}

bool PowerHandler::reopenDevice()
{
    // Close current file descriptor if open
    if (m_device_fd >= 0)
    {
        close(m_device_fd);
        m_device_fd = -1;
    }

    // Try to reopen the device
    m_device_fd = findPowerDevice();
    if (m_device_fd < 0)
    {
        DEBUG_ERR("Failed to reopen power input device");
        return false;
    }

    DEBUG_LOG("Power handler device reopened successfully (dynamic discovery)");
    flushEvents(); // Use the simplified flush function

    return true;
}
