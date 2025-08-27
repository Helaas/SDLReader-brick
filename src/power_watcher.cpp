#include "power_watcher.h"

#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <optional>
#include <cstdlib> // getenv
#include <cstdio>
#include <string>
#include <vector>
#include <time.h> // nanosleep

using namespace std::chrono;

namespace
{
    constexpr int MAX_EVENTS = 8;
    constexpr int EPOLL_TIMEOUT_MS = 250;

    bool hasBit(const uint8_t *arr, size_t idx)
    {
        return (arr[idx / 8] >> (idx % 8)) & 1;
    }

    inline bool pw_debug_enabled()
    {
        return true;
        static int v = []
        {
            const char *e = std::getenv("PW_DEBUG");
            return (e && *e && *e != '0') ? 1 : 0;
        }();
        return v != 0;
    }

    // Monotonic ms since process start for readable timestamps
    inline uint64_t pw_ms()
    {
        static const auto t0 = steady_clock::now();
        return duration_cast<milliseconds>(steady_clock::now() - t0).count();
    }

    // Choose log destination once (env PW_LOGFILE), fallback to stderr
    FILE *pw_log_file()
    {
        static FILE *log_file = nullptr;
        if (!log_file) {
            log_file = std::fopen("/tmp/power_watcher.log", "a"); // Use append mode
        }
        return log_file;
    }

    // Lightweight printf-style logger gated by PW_DEBUG
    template <typename... Args>
    inline void PW_LOG(const char *fmt, Args... args)
    {
        if (!pw_debug_enabled())
            return;
        FILE *f = pw_log_file();
        std::fprintf(f, "[PW %8llums] ", (unsigned long long)pw_ms());
        std::fprintf(f, fmt, args...);
        std::fprintf(f, "\n");
        std::fflush(f);
    }

    // Read a whole small text file; returns empty string on error
    std::string readSmallTextFile(const char *path)
    {
        std::string out;
        int fd = ::open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
        {
            PW_LOG("read: open(%s) failed errno=%d (%s)", path, errno, std::strerror(errno));
            return out;
        }
        char buf[512];
        for (;;)
        {
            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n > 0)
                out.append(buf, buf + n);
            if (n == 0)
                break;
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                PW_LOG("read: read(%s) failed errno=%d (%s)", path, errno, std::strerror(errno));
                break;
            }
        }
        ::close(fd);
        // trim trailing newlines/spaces for readability
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t'))
            out.pop_back();
        return out;
    }

    // Low-level write with retries for EINTR/EBUSY
    bool writeFileWithRetries(const char *path, const char *data, size_t len)
    {
        int fd = ::open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0)
        {
            PW_LOG("writeFile: open(%s) failed errno=%d (%s)", path, errno, std::strerror(errno));
            return false;
        }

        const int kMaxRetries = 3;
        ssize_t w = -1;
        int last_errno = 0;

        for (int attempt = 0; attempt < kMaxRetries; ++attempt)
        {
            w = ::write(fd, data, len);
            if (w == (ssize_t)len)
                break;
            last_errno = errno;
            PW_LOG("writeFile: write(%s) attempt %d failed w=%zd len=%zu errno=%d (%s)",
                   path, attempt + 1, w, len, last_errno, std::strerror(last_errno));
            if (last_errno != EINTR && last_errno != EBUSY)
                break;
            // small backoff ~50ms
            struct timespec ts{0, 50 * 1000 * 1000};
            nanosleep(&ts, nullptr);
        }

        ::close(fd);

        if (w == (ssize_t)len)
        {
            PW_LOG("writeFile: wrote %zd/%zu bytes to %s", w, len, path);
            return true;
        }
        else
        {
            PW_LOG("writeFile: final failure for %s (w=%zd len=%zu errno=%d: %s)",
                   path, w, len, last_errno ? last_errno : errno, std::strerror(last_errno ? last_errno : errno));
            return false;
        }
    }

} // anonymous namespace

PowerWatcher::PowerWatcher()
{
    m_eventType = SDL_RegisterEvents(1);
}

PowerWatcher::~PowerWatcher()
{
    stop();
}

int PowerWatcher::openRO(const std::string &path)
{
    return ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
}

static bool readEvBits(int fd, uint8_t *dst, size_t nbytes, int ev)
{
    return ioctl(fd, EVIOCGBIT(ev, nbytes), dst) >= 0;
}

bool PowerWatcher::pathSupportsPowerKey(const std::string &devPath)
{
    int fd = openRO(devPath);
    if (fd < 0)
        return false;

    uint8_t ev_bits[(EV_MAX + 7) / 8]{};
    if (!readEvBits(fd, ev_bits, sizeof(ev_bits), 0))
    {
        close(fd);
        return false;
    }
    if (!hasBit(ev_bits, EV_KEY))
    {
        close(fd);
        return false;
    }

    uint8_t key_bits[(KEY_MAX + 7) / 8]{};
    if (!readEvBits(fd, key_bits, sizeof(key_bits), EV_KEY))
    {
        close(fd);
        return false;
    }

    bool ok = hasBit(key_bits, KEY_POWER);
    close(fd);
    return ok;
}

bool PowerWatcher::openDevices(const std::string &hint)
{
    return openDevices(hint, true); // Default to arming fence
}

bool PowerWatcher::openDevices(const std::string &hint, bool arm_fence)
{
    PW_LOG("openDevices: starting with hint='%s' arm_fence=%d", hint.c_str(), arm_fence);
    m_fds.clear();

    auto tryOpen = [&](const std::string &path)
    {
        if (!pathSupportsPowerKey(path))
            return;
        int fd = openRO(path);
        if (fd >= 0)
        {
            m_fds.push_back({fd, path});

            // Use monotonic timestamps (more robust across suspend/resume)
            int clk = CLOCK_MONOTONIC;
            ioctl(fd, EVIOCSCLOCKID, &clk);

            int one = 1;
            ioctl(fd, EVIOCGRAB, &one); // ignore errors if unsupported

            // Drop any buffered events so we start clean.
            PowerWatcher::drain_fd(fd);

            char name[256] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0)
                SDL_Log("PowerWatcher: opened %s (name=\"%s\")", path.c_str(), name);
            else
                SDL_Log("PowerWatcher: opened %s", path.c_str());

            PW_LOG("openDevices: added fd=%d path=%s", fd, path.c_str());
        }
    };

    if (!hint.empty())
        tryOpen(hint);

    if (m_fds.empty())
    {
        if (DIR *d = opendir("/dev/input"))
        {
            while (dirent *e = readdir(d))
            {
                std::string name = e->d_name;
                if (name.rfind("event", 0) == 0)
                    tryOpen(std::string("/dev/input/") + name);
            }
            closedir(d);
        }
    }

    // Swallow the first press+release cycle after (re)open
    if (!m_fds.empty() && arm_fence)
    {
        m_swallow_next_cycle.store(true, std::memory_order_relaxed);
        PW_LOG("openDevices: swallow fence ARMED");
        
        // Additional stabilization delay for device opening
        SDL_Delay(100);
    }
    else if (!m_fds.empty())
    {
        PW_LOG("openDevices: fence NOT armed (arm_fence=false)");
    }
    
    PW_LOG("openDevices: found %zu devices", m_fds.size());
    return !m_fds.empty();
}

bool PowerWatcher::start(const PowerWatcherConfig &cfg)
{
    stop();
    m_cfg = cfg;
    if (!openDevices(cfg.device_hint, true))  // Arm fence on initial startup
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "PowerWatcher: no input device with KEY_POWER found under /dev/input");
        return false;
    }
    m_running = true;
    m_thr = std::thread(&PowerWatcher::threadMain, this);
    return true;
}

void PowerWatcher::stop()
{
    if (!m_running.exchange(false))
        return;
    if (m_thr.joinable())
        m_thr.join();
    closeAll();
}

void PowerWatcher::closeAll()
{
    for (auto &f : m_fds)
        if (f.fd >= 0)
        {
            // If you used EVIOCGRAB above, release it here:
            // int zero = 0; ioctl(f.fd, EVIOCGRAB, &zero);
            ::close(f.fd);
            PW_LOG("closeAll: closed fd=%d path=%s", f.fd, f.path.c_str());
        }
    m_fds.clear();
}

void PowerWatcher::postEvent(PowerEventType which)
{
    if (m_cfg.on_event)
        m_cfg.on_event(which);

    if (m_eventType != (Uint32)-1)
    {
        SDL_Event ev{};
        ev.type = m_eventType;
        ev.user.code = static_cast<Sint32>(which);
        ev.user.data1 = this;
        ev.user.data2 = nullptr;
        SDL_PushEvent(&ev);
    }
}

void PowerWatcher::threadMain()
{
    PW_LOG("threadMain: starting with %zu devices", m_fds.size());
    
    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0)
    {
        m_running = false;
        return;
    }

    for (auto &f : m_fds)
    {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = f.fd;
        epoll_ctl(ep, EPOLL_CTL_ADD, f.fd, &ev);
        PW_LOG("epoll ADD fd=%d", f.fd);
    }

    // On startup, swallow the first power-button cycle
    m_swallow_next_cycle.store(true, std::memory_order_relaxed);
    
    // Add periodic device validation (like NextUI)
    auto last_device_check = steady_clock::now();
    auto last_device_reopen = steady_clock::now(); // Track last reopen to avoid thrashing
    constexpr auto DEVICE_CHECK_INTERVAL = std::chrono::seconds(30); // Reduced frequency to avoid interference
    constexpr auto MIN_REOPEN_INTERVAL = std::chrono::seconds(10); // Minimum time between reopens

    // --- Edge-balanced swallow fence: wait for release, then short grace window ---
    enum class FenceState
    {
        Idle,
        SwallowUntilRelease,
        Grace
    };
    FenceState fence = FenceState::SwallowUntilRelease;
    auto fence_armed_at = steady_clock::now();
    const auto fence_grace = milliseconds(50); // Shorter grace period for faster response
    const auto fence_timeout = milliseconds(500); // Max time to wait for release event (reduced from 1000ms)

    bool pressed = false;
    bool long_fired = false;
    auto t_down = steady_clock::now();
    auto last_tick = steady_clock::now();
    auto last_resume_time = steady_clock::now(); // Track when we last resumed - initialize to startup

    while (m_running)
    {
        // Periodic device validation (similar to NextUI approach) - TEMPORARILY DISABLED
        auto now_time = steady_clock::now();
        if (false && now_time - last_device_check > DEVICE_CHECK_INTERVAL) // DISABLED
        {
            PW_LOG("Running device validation check");
            last_device_check = now_time;
            bool all_devices_valid = true;
            for (auto &f : m_fds)
            {
                // Test if device fd is still valid (gentler than access())
                int flags = fcntl(f.fd, F_GETFL);
                if (flags == -1)
                {
                    PW_LOG("Device validation failed for fd=%d path=%s (fcntl failed)", f.fd, f.path.c_str());
                    all_devices_valid = false;
                    break;
                }
            }
            
            if (!all_devices_valid && (now_time - last_device_reopen > MIN_REOPEN_INTERVAL))
            {
                PW_LOG("Device validation failed, reopening devices");
                SDL_Log("PowerWatcher: Device validation failed, reopening");
                last_device_reopen = now_time;
                // Force device reopening
                for (auto &f : m_fds)
                {
                    if (f.fd >= 0)
                    {
                        epoll_ctl(ep, EPOLL_CTL_DEL, f.fd, nullptr);
                        ::close(f.fd);
                    }
                }
                m_fds.clear();
                
                if (openDevices(m_cfg.device_hint, false)) // Don't arm fence automatically
                {
                    for (auto &f : m_fds)
                    {
                        epoll_event ev{};
                        ev.events = EPOLLIN;
                        ev.data.fd = f.fd;
                        epoll_ctl(ep, EPOLL_CTL_ADD, f.fd, &ev);
                    }
                    // Only arm fence if we haven't done so recently
                    if (!m_swallow_next_cycle.load(std::memory_order_relaxed))
                    {
                        PW_LOG("Arming fence after device validation reopen");
                        m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                        fence = FenceState::SwallowUntilRelease;
                        fence_armed_at = steady_clock::now();
                        last_resume_time = steady_clock::now();
                    }
                    else
                    {
                        PW_LOG("Skipping fence re-arm - already active");
                    }
                }
            }
            else if (!all_devices_valid)
            {
                PW_LOG("Device validation failed but skipping reopen (too soon since last reopen)");
            }
            else
            {
                PW_LOG("Device validation passed - all devices OK");
            }
        }

        // Resume-gap heuristic: reopen and re-arm fence after a long epoll gap - TEMPORARILY DISABLED
        auto now_tick = steady_clock::now();
        auto gap_ms = duration_cast<milliseconds>(now_tick - last_tick).count();
        if (false && gap_ms > 2000 && // DISABLED - testing if this causes fence issues
            (now_tick - last_device_reopen > MIN_REOPEN_INTERVAL)) // Don't reopen too frequently
        {
            PW_LOG("resume-gap detected: %lldms gap, reopening devices", (long long)gap_ms);
            last_device_reopen = now_tick;
            for (auto &f : m_fds)
                if (f.fd >= 0)
                {
                    epoll_ctl(ep, EPOLL_CTL_DEL, f.fd, nullptr);
                    ::close(f.fd);
                }
            m_fds.clear();

            if (openDevices(m_cfg.device_hint, false))  // Don't arm fence on reopen
            {
                for (auto &f : m_fds)
                {
                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.fd = f.fd;
                    epoll_ctl(ep, EPOLL_CTL_ADD, f.fd, &ev);
                    
                    // Extra drain after reopening to clear any pending events
                    PowerWatcher::drain_fd(f.fd);
                }
            }
            // Only arm fence if not already active
            if (!m_swallow_next_cycle.load(std::memory_order_relaxed))
            {
                PW_LOG("Arming fence after gap reopen");
                m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                fence = FenceState::SwallowUntilRelease;
                fence_armed_at = steady_clock::now();
                pressed = false;
                long_fired = false;
                last_resume_time = steady_clock::now(); // Track resume time
            }
            else
            {
                PW_LOG("Skipping fence re-arm after gap - already active");
            }
        }
        last_tick = now_tick;

        // Check for fence timeout even when no events are coming in
        if (m_swallow_next_cycle.load(std::memory_order_relaxed) && 
            fence == FenceState::SwallowUntilRelease)
        {
            if (duration_cast<milliseconds>(steady_clock::now() - fence_armed_at) >= fence_timeout)
            {
                PW_LOG("fence: timeout in main loop, clearing fence");
                m_swallow_next_cycle.store(false, std::memory_order_relaxed);
                fence = FenceState::Idle;
                pressed = false;
                long_fired = false;
            }
        }

        bool need_reopen = false;

        // Debug: log every main loop iteration
        static int loop_count = 0;
        if (++loop_count % 100 == 0) // Every 100 loops
        {
            PW_LOG("Main loop iteration %d", loop_count);
        }

        epoll_event evs[MAX_EVENTS];
        int n = epoll_wait(ep, evs, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            PW_LOG("epoll_wait error=%d", errno);
            break;
        }
        
        if (n > 0)
        {
            PW_LOG("epoll_wait returned %d events", n);
        }

        for (int i = 0; i < n; ++i)
        {
            PW_LOG("epoll event: fd=%d events=0x%x", evs[i].data.fd, evs[i].events);
            
            if ((evs[i].events & (EPOLLERR | EPOLLHUP)) != 0)
            {
                need_reopen = true;
                PW_LOG("epoll HUP/ERR on fd=%d", evs[i].data.fd);
                continue;
            }

            int fd = evs[i].data.fd;
            input_event iev{};
            for (;;)
            {
                ssize_t r = ::read(fd, &iev, sizeof(iev));
                if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                if (r != sizeof(iev))
                {
                    need_reopen = true;
                    break;
                }

                // Log all input events for debugging
                if (iev.type == EV_KEY)
                {
                    PW_LOG("input event: type=%d code=%d value=%d (KEY_POWER=%d)", 
                           iev.type, iev.code, iev.value, KEY_POWER);
                }

                if (iev.type == EV_SYN && iev.code == SYN_DROPPED)
                {
                    need_reopen = true;
                    continue;
                }

                if (iev.type != EV_KEY || iev.code != KEY_POWER)
                    continue;

                PW_LOG("event: val=%d pressed=%d fence=%d long=%d", iev.value, pressed, (int)fence, long_fired);

                // Ignore events that come too soon after resume (spurious wake events)
                const auto min_time_since_resume = milliseconds(500); // 500ms minimum to avoid spurious events
                if (duration_cast<milliseconds>(steady_clock::now() - last_resume_time) < min_time_since_resume)
                {
                    PW_LOG("ignoring spurious event too soon after resume");
                    continue;
                }

                // Swallow one post-resume cycle
                if (m_swallow_next_cycle.load(std::memory_order_relaxed))
                {
                    if (fence == FenceState::SwallowUntilRelease)
                    {
                        // Check for timeout - if we've been waiting too long for a release, give up
                        if (duration_cast<milliseconds>(steady_clock::now() - fence_armed_at) >= fence_timeout)
                        {
                            PW_LOG("fence: timeout waiting for release, clearing fence");
                            m_swallow_next_cycle.store(false, std::memory_order_relaxed);
                            fence = FenceState::Idle;
                            pressed = false;
                            long_fired = false;
                        }
                        else if (iev.value == 0) // release ends wake spike
                        {
                            PW_LOG("fence: saw release, entering Grace");
                            fence = FenceState::Grace;
                            fence_armed_at = steady_clock::now();
                        }
                        else
                        {
                            continue; // ignore until we see a real release or timeout
                        }
                    }
                    else if (fence == FenceState::Grace)
                    {
                        if (duration_cast<milliseconds>(steady_clock::now() - fence_armed_at) >= fence_grace)
                        {
                            m_swallow_next_cycle.store(false, std::memory_order_relaxed);
                            fence = FenceState::Idle;
                            PW_LOG("fence: Grace expired, now Idle");
                            pressed = false;
                            long_fired = false;
                        }
                        else
                        {
                            continue; // still ignoring during grace
                        }
                    }
                }

                // Ignore auto-repeat
                if (iev.value == 2)
                    continue;

                // Normal state machine
                if (iev.value == 1)
                { // press
                    pressed = true;
                    long_fired = false;
                    t_down = steady_clock::now();
                    PW_LOG("press detected");
                }
                else if (iev.value == 0 && pressed)
                { // release - but only if we have a corresponding press
                    pressed = false;
                    auto ms = duration_cast<milliseconds>(steady_clock::now() - t_down).count();
                    PW_LOG("release after %lld ms", (long long)ms);

                    if (!long_fired && ms >= m_cfg.short_min_ms)
                    {
                        PW_LOG("=== SLEEP TEST START ===");
                        std::fflush(stdout); std::fflush(stderr);
                        
                        // Record time before sleep attempt
                        auto start_time = steady_clock::now();
                        
                        // Test 1: Read available states
                        std::string available = readSmallTextFile("/sys/power/state");
                        PW_LOG("States: [%s]", available.c_str());
                        std::fflush(stdout); std::fflush(stderr);
                        
                        // Test 2: Try both sleep modes and system diagnostics
                        PW_LOG("Checking system readiness...");
                        
                        // Check if system is currently trying to suspend
                        std::string pm_trace = readSmallTextFile("/sys/power/pm_trace");
                        if (!pm_trace.empty()) {
                            PW_LOG("pm_trace: %s", pm_trace.c_str());
                        }
                        
                        // Try mem mode first (deeper sleep)
                        PW_LOG("Attempt 1: mem mode");
                        int fd = ::open("/sys/power/state", O_WRONLY | O_NONBLOCK);
                        if (fd >= 0) {
                            const char *cmd = "mem\n";
                            errno = 0; // Clear errno before write
                            ssize_t w = ::write(fd, cmd, strlen(cmd));
                            int err = errno;
                            ::close(fd);
                            PW_LOG("Mem write: %zd err=%d", w, err);
                            
                            if (w == (ssize_t)strlen(cmd) && err == 0) {
                                PW_LOG("SUCCESS: mem mode worked! (wrote %zd bytes)", w);
                                // SUCCESS! Arm fence to prevent spurious wake events  
                                PW_LOG("Arming fence - sleep succeeded, preventing spurious wake events");
                                m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                                fence = FenceState::SwallowUntilRelease;
                                fence_armed_at = steady_clock::now();
                                last_resume_time = steady_clock::now();
                                bool ok = true; // Success!
                            }
                        }
                        
                        // If mem failed, try freeze
                        PW_LOG("Attempt 2: freeze mode");
                        fd = ::open("/sys/power/state", O_WRONLY | O_NONBLOCK);
                        if (fd >= 0) {
                            const char *cmd = "freeze\n";
                            errno = 0; // Clear errno before write
                            ssize_t w = ::write(fd, cmd, strlen(cmd));
                            int err = errno;
                            ::close(fd);
                            PW_LOG("Freeze write: %zd err=%d", w, err);
                            
                            if (w == (ssize_t)strlen(cmd) && err == 0) {
                                PW_LOG("SUCCESS: freeze mode worked! (wrote %zd bytes)", w);
                                // SUCCESS! Arm fence to prevent spurious wake events
                                PW_LOG("Arming fence - sleep succeeded, preventing spurious wake events");
                                m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                                fence = FenceState::SwallowUntilRelease;
                                fence_armed_at = steady_clock::now();
                                last_resume_time = steady_clock::now();
                                bool ok = true; // Success!
                            }
                        } else {
                            PW_LOG("Open failed: %d", errno);
                        }
                        std::fflush(stdout); std::fflush(stderr);
                        
                        // Check if any time passed (indicating possible sleep/wake cycle)
                        auto end_time = steady_clock::now();
                        auto elapsed = duration_cast<milliseconds>(end_time - start_time).count();
                        PW_LOG("Total test time: %lld ms", (long long)elapsed);
                        
                        PW_LOG("=== SLEEP TEST END ===");
                        bool ok = false;
                        PW_LOG("deep sleep request %s (errno=%d: %s)",
                               ok ? "OK" : "FAILED", errno, std::strerror(errno));
                        if (ok)
                        {
                            // Only arm the post-resume swallow if we actually initiated suspend
                            m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                            fence = FenceState::SwallowUntilRelease;
                            fence_armed_at = steady_clock::now();
                            last_resume_time = steady_clock::now(); // Will resume here
                            
                            // Extended settle time after sleep request
                            SDL_Delay(200);
                        }
                        else
                        {
                            // Do NOT arm the fence; let next press try again
                        }
                        postEvent(PowerEventType::ShortPress);
                    }
                }
                else if (iev.value == 0 && !pressed)
                {
                    // Orphaned release event (release without press) - ignore it
                    PW_LOG("ignoring orphaned release event (no corresponding press)");
                    continue;
                }

                // Long press -> shutdown
                if (pressed && !long_fired)
                {
                    auto held_ms = duration_cast<milliseconds>(steady_clock::now() - t_down).count();
                    if (held_ms >= m_cfg.long_press_ms)
                    {
                        PW_LOG("long press -> shutdown (requesting)");
                        bool ok = PowerWatcher::requestShutdown();
                        PW_LOG("shutdown request %s (errno=%d: %s)",
                               ok ? "OK" : "FAILED", errno, std::strerror(errno));
                        postEvent(PowerEventType::LongPress);
                        long_fired = true;
                    }
                }
            }
        }

        if (need_reopen)
        {
            PW_LOG("need_reopen triggered");
            
            // Check if we reopened recently to avoid thrashing
            if (duration_cast<milliseconds>(steady_clock::now() - last_device_reopen) < MIN_REOPEN_INTERVAL)
            {
                PW_LOG("Skipping reopen - too soon since last reopen");
                (void)epoll_wait(ep, nullptr, 0, 150);
                continue;
            }
            
            last_device_reopen = steady_clock::now();
            for (auto &f : m_fds)
                if (f.fd >= 0)
                {
                    epoll_ctl(ep, EPOLL_CTL_DEL, f.fd, nullptr);
                    ::close(f.fd);
                }
            m_fds.clear();

            if (openDevices(m_cfg.device_hint, false))  // Don't arm fence on reopen
            {
                for (auto &f : m_fds)
                {
                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.fd = f.fd;
                    epoll_ctl(ep, EPOLL_CTL_ADD, f.fd, &ev);
                    
                    // Extra drain after reopening to clear any pending events
                    PowerWatcher::drain_fd(f.fd);
                }
                // Only arm fence if not already active
                if (!m_swallow_next_cycle.load(std::memory_order_relaxed))
                {
                    PW_LOG("Arming fence after error reopen");
                    m_swallow_next_cycle.store(true, std::memory_order_relaxed);
                    fence = FenceState::SwallowUntilRelease;
                    fence_armed_at = steady_clock::now();
                    pressed = false;
                    long_fired = false;
                    last_resume_time = steady_clock::now(); // Track resume time
                }
                else
                {
                    PW_LOG("Skipping fence re-arm after error - already active");
                }
                
                // Longer ignore period after device reopening (like NextUI)
                SDL_Delay(500);
            }
            else
            {
                (void)epoll_wait(ep, nullptr, 0, 150);
            }
        }
    }

    ::close(ep);
}

// --- helpers ---
bool PowerWatcher::writeFile(const char *path, const char *data, size_t len)
{
    return writeFileWithRetries(path, data, len);
}

bool PowerWatcher::execCmd(const char *path)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        PW_LOG("execCmd: fork failed");
        return false;
    }
    if (pid == 0)
    {
        execl(path, path, (char *)nullptr);
        _exit(127);
    }
    PW_LOG("execCmd: spawned %s pid=%d", path, pid);
    return true;
}

bool PowerWatcher::requestDeepSleep()
{
    // Note: This function is kept for API compatibility but the actual sleep
    // logic is handled inline in the main event loop for better responsiveness
    PW_LOG("requestDeepSleep() called - sleep is handled inline in event loop");
    return false; // Always return false to maintain existing behavior
}

bool PowerWatcher::requestShutdown()
{
    if (access("/sbin/poweroff", X_OK) == 0)
        return execCmd("/sbin/poweroff");
    if (access("/bin/poweroff", X_OK) == 0)
        return execCmd("/bin/poweroff");
    if (access("/usr/bin/systemctl", X_OK) == 0)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            execl("/usr/bin/systemctl", "/usr/bin/systemctl", "poweroff", (char *)nullptr);
            _exit(127);
        }
        PW_LOG("=== requestDeepSleep() SUCCESS - fallback mode worked ===");
        return true;
    }
    return false;
}

void PowerWatcher::drain_fd(int fd)
{
    input_event ev;
    for (;;)
    {
        ssize_t n = ::read(fd, &ev, sizeof(ev));
        if (n == sizeof(ev))
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (n <= 0)
            break; // other error/EOF
    }
}
