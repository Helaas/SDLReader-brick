#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <SDL.h>

enum class PowerEventType { ShortPress = 1, LongPress = 2 };

struct PowerWatcherConfig {
    int long_press_ms = 1000;
    std::string device_hint;
    std::function<void(PowerEventType)> on_event;
    int short_min_ms = 8;   // minimal, snappy debounce
};

class PowerWatcher {
public:
    PowerWatcher();
    ~PowerWatcher();

    bool start(const PowerWatcherConfig &cfg = {});
    void stop();

    Uint32 sdlEventType() const { return m_eventType; }

    static bool requestDeepSleep();
    static bool requestShutdown();

    // Swallow the next power-button press/release cycle (use after resume/focus if desired)
    inline void armResumeFence() {
        m_swallow_next_cycle.store(true, std::memory_order_relaxed);
    }
    
    // More robust resume kick with extended ignore period
    inline void resumeKick(int ignore_ms = 800) {
        m_swallow_next_cycle.store(true, std::memory_order_relaxed);
        // Additional delay to allow system stabilization
        SDL_Delay(ignore_ms);
    }
    
    // Force clear the fence (emergency escape hatch)
    inline void clearFence() {
        m_swallow_next_cycle.store(false, std::memory_order_relaxed);
    }

    static void drain_fd(int fd);

    static inline int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

private:
    struct FD { int fd = -1; std::string path; };

    bool openDevices(const std::string &hint);
    bool openDevices(const std::string &hint, bool arm_fence);
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

    // Simple, robust fence: swallow exactly one cycle after (re)open/resume
    std::atomic<bool> m_swallow_next_cycle{false};
};
