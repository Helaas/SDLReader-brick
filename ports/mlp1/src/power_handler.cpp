#include "power_handler.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include "debug_log.h"
#include <linux/input.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

PowerHandler::PowerHandler() : m_running(false), m_device_fd(-1)
{
}

PowerHandler::~PowerHandler()
{
    stop();
}

int PowerHandler::findPowerDevice()
{
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

    DEBUG_LOG("Power handler started (read-only observer mode)");
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

    DEBUG_LOG("Power handler thread started (MLP1 read-only mode)");

    while (m_running.load())
    {
        checkLongPressWhileHeld();
        ssize_t bytes_read = read(m_device_fd, &ev, sizeof(ev));

        if (bytes_read == sizeof(ev))
        {
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
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    if (duration >= LONG_PRESS_DURATION)
    {
        DEBUG_LOG("PowerHandler: Long press detected (" << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << "ms) - shutting down");
        m_longPressHandled = true;
        m_powerButtonDown = false;
        requestShutdown();
    }
}

void PowerHandler::handlePowerButtonEvent(const input_event& ev)
{
    auto now = std::chrono::steady_clock::now();

    // Post-resume ignore window
    if (m_resume_ignore_until != std::chrono::steady_clock::time_point{} &&
        now < m_resume_ignore_until)
    {
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
        // Button pressed - start tracking for long press
        DEBUG_LOG("Power button pressed");
        m_powerButtonDown = true;
        m_longPressHandled = false;
        m_powerPressTime = now;
    }
    else if (ev.value == 0 && m_powerPressTime != std::chrono::steady_clock::time_point{})
    {
        // Button released
        m_powerButtonDown = false;
        m_longPressHandled = false;
        m_powerPressTime = std::chrono::steady_clock::time_point{};
    }
    else if (ev.value == 2)
    {
        checkLongPressWhileHeld();
    }
}

void PowerHandler::attemptStandby()
{
    DEBUG_LOG("PowerHandler: Attempting standby via loong SDK...");

    if (m_preSleepCallback)
    {
        m_preSleepCallback();
    }

    if (sleepViaLoongSdk())
    {
        DEBUG_LOG("PowerHandler: loong SDK standby successful");
        m_resume_ignore_until = std::chrono::steady_clock::now() + POST_RESUME_IGNORE_DURATION;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flushEvents();
        return;
    }

    DEBUG_LOG("PowerHandler: loong SDK standby failed, trying sysfs fallback");
    if (sleepViaSysfs())
    {
        DEBUG_LOG("PowerHandler: sysfs standby successful");
        m_resume_ignore_until = std::chrono::steady_clock::now() + POST_RESUME_IGNORE_DURATION;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        flushEvents();
        return;
    }

    DEBUG_ERR("PowerHandler: All suspend methods failed");
    if (m_errorCallback)
    {
        m_errorCallback("Standby failed");
    }
}

bool PowerHandler::sleepViaLoongSdk()
{
    // dlopen /usr/lib/libloong_sdk.so
    // Use PowerApi::get() -> PowerApi::powerStandby()
    void* handle = dlopen("/usr/lib/libloong_sdk.so", RTLD_LAZY | RTLD_LOCAL);
    if (!handle)
    {
        DEBUG_LOG("loong SDK not available: " << (dlerror() ? dlerror() : "unknown"));
        return false;
    }

    // Symbol: _ZN5loong8PowerApi3getEv -> loong::PowerApi::get()
    typedef void* (*PowerGetFunc)();
    PowerGetFunc s_power_get = reinterpret_cast<PowerGetFunc>(dlsym(handle, "_ZN5loong8PowerApi3getEv"));
    if (!s_power_get)
    {
        DEBUG_LOG("loong PowerApi::get() not found");
        dlclose(handle);
        return false;
    }

    // Symbol: _ZN5loong8PowerApi12powerStandbyEv -> loong::PowerApi::powerStandby()
    typedef void (*PowerStandbyFunc)(void*);
    PowerStandbyFunc s_power_standby = reinterpret_cast<PowerStandbyFunc>(dlsym(handle, "_ZN5loong8PowerApi12powerStandbyEv"));
    if (!s_power_standby)
    {
        DEBUG_LOG("loong PowerApi::powerStandby() not found");
        dlclose(handle);
        return false;
    }

    void* api = s_power_get();
    s_power_standby(api);
    dlclose(handle);
    return true;
}

bool PowerHandler::sleepViaSysfs()
{
    if (access("/sys/power/state", W_OK) == 0)
    {
        int result = system("echo mem > /sys/power/state 2>/dev/null");
        if (result == 0)
        {
            return true;
        }
    }

    return false;
}

void PowerHandler::requestShutdown()
{
    DEBUG_LOG("PowerHandler: Shutting down...");

    if (m_errorCallback)
    {
        m_errorCallback("Shutting down...");
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    sync();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    system("/usr/sbin/poweroff");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    DEBUG_LOG("PowerHandler: poweroff call returned, exiting application");
    std::exit(0);
}

bool PowerHandler::reopenDevice()
{
    if (m_device_fd >= 0)
    {
        close(m_device_fd);
        m_device_fd = -1;
    }

    m_device_fd = findPowerDevice();
    if (m_device_fd < 0)
    {
        DEBUG_ERR("Failed to reopen power input device");
        return false;
    }

    DEBUG_LOG("Power handler device reopened successfully");
    flushEvents();
    return true;
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
            break;
    }
}
