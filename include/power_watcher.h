#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <SDL.h>

enum class PowerEventType
{
    ShortPress = 1,
    LongPress = 2
};

struct PowerWatcherConfig
{
    // Duration (ms) that qualifies as a "long" press (default 2000ms)
    int long_press_ms = 2000;
    // Optional: path hint to a specific input node (e.g., "/dev/input/by-path/…-gpio-keys-event")
    std::string device_hint;
    // Optional callback fired on press classification (in addition to the SDL event)
    std::function<void(PowerEventType)> on_event;
};

class PowerWatcher
{
public:
    PowerWatcher();
    ~PowerWatcher();

    // Start background watcher; returns false if it couldn't open any input device
    bool start(const PowerWatcherConfig &cfg = {});
    void stop();

    // The custom SDL event type this watcher posts (treat like any other SDL event)
    Uint32 sdlEventType() const { return m_eventType; }

    // Convenience: perform system actions (best-effort; return true on success)
    static bool requestDeepSleep(); // tries: echo mem > /sys/power/state
    static bool requestShutdown();  // tries: /sbin/poweroff

    // Public: called  when the app regains focus or after resume.
    // The watcher will ignore key events for 'cooldown_ms' to avoid wake key noise.
    void resumeKick(int cooldown_ms = 300);

private:
    struct FD
    {
        int fd = -1;
        std::string path;
    };
    bool openDevices(const std::string &hint);
    void threadMain();
    void postEvent(PowerEventType which);
    void closeAll();
    static bool pathSupportsPowerKey(const std::string &devPath);
    static int openRO(const std::string &path);
    static bool writeFile(const char *path, const char *data, size_t len);
    static bool execCmd(const char *path);

    PowerWatcherConfig m_cfg{};
    std::vector<FD> m_fds;
    std::thread m_thr;
    std::atomic<bool> m_running{false};
    Uint32 m_eventType{0};
    std::atomic<long long> m_ignore_until{0}; // monotonic ms timestamp
    static long long now_ms();
};
