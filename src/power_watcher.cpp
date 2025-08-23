#include "power_watcher.h"

#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace std::chrono;

namespace
{
    constexpr int MAX_EVENTS = 8;
    constexpr int EPOLL_TIMEOUT_MS = 250;

    bool hasBit(const uint8_t *arr, size_t idx)
    {
        return (arr[idx / 8] >> (idx % 8)) & 1;
    }
} // namespace

static inline long long mono_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

long long PowerWatcher::now_ms() { return mono_ms(); }

void PowerWatcher::resumeKick(int cooldown_ms)
{
    m_ignore_until.store(now_ms() + cooldown_ms, std::memory_order_relaxed);
}

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

bool PowerWatcher::pathSupportsPowerKey(const std::string &devPath)
{
    int fd = openRO(devPath);
    if (fd < 0)
        return false;

    uint8_t ev_bits[(EV_MAX + 7) / 8]{};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits) < 0)
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
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0)
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
    m_fds.clear();

    auto tryOpen = [&](const std::string &path)
    {
        if (!pathSupportsPowerKey(path))
            return;
        int fd = openRO(path);
        if (fd >= 0)
        {
            m_fds.push_back({fd, path});
            char name[256] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0)
            {
                SDL_Log("PowerWatcher: opened %s (name=\"%s\")", path.c_str(), name);
            }
            else
            {
                SDL_Log("PowerWatcher: opened %s", path.c_str());
            }
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
                {
                    tryOpen(std::string("/dev/input/") + name);
                }
            }
            closedir(d);
        }
    }

    return !m_fds.empty();
}

bool PowerWatcher::start(const PowerWatcherConfig &cfg)
{
    stop();
    m_cfg = cfg;
    if (!openDevices(cfg.device_hint))
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
            ::close(f.fd);
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
    }

    bool pressed = false;
    steady_clock::time_point t_down{};

    while (m_running)
    {
        // Post-resume cooldown
        if (now_ms() < m_ignore_until.load(std::memory_order_relaxed))
        {
            pressed = false;
            (void)epoll_wait(ep, nullptr, 0, 50);
            continue;
        }

        bool need_reopen = false;

        epoll_event evs[MAX_EVENTS];
        int n = epoll_wait(ep, evs, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            if ((evs[i].events & (EPOLLERR | EPOLLHUP)) != 0)
            {
                need_reopen = true;
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

                if (iev.type == EV_SYN && iev.code == SYN_DROPPED)
                {
                    need_reopen = true;
                    continue;
                }

                if (iev.type == EV_KEY && iev.code == KEY_POWER)
                {
                    if (iev.value == 1)
                    {
                        pressed = true;
                        t_down = steady_clock::now();
                    }
                    else if (iev.value == 0 && pressed)
                    {
                        pressed = false;
                        auto ms = duration_cast<milliseconds>(
                                      steady_clock::now() - t_down)
                                      .count();
                        if (ms >= m_cfg.long_press_ms)
                            postEvent(PowerEventType::LongPress);
                        else
                            postEvent(PowerEventType::ShortPress);
                    }
                }
            }
        }

        if (need_reopen)
        {
            for (auto &f : m_fds)
                if (f.fd >= 0)
                {
                    epoll_ctl(ep, EPOLL_CTL_DEL, f.fd, nullptr);
                    ::close(f.fd);
                }
            m_fds.clear();

            if (openDevices(m_cfg.device_hint))
            {
                for (auto &f : m_fds)
                {
                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.fd = f.fd;
                    epoll_ctl(ep, EPOLL_CTL_ADD, f.fd, &ev);
                }
                m_ignore_until.store(now_ms() + 300, std::memory_order_relaxed);
                pressed = false;
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
    int fd = ::open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    ssize_t w = ::write(fd, data, len);
    ::close(fd);
    return (w == (ssize_t)len);
}

bool PowerWatcher::execCmd(const char *path)
{
    pid_t pid = fork();
    if (pid < 0)
        return false;
    if (pid == 0)
    {
        execl(path, path, (char *)nullptr);
        _exit(127);
    }
    return true;
}

bool PowerWatcher::requestDeepSleep()
{
    static const char *p = "/sys/power/state";
    return writeFile(p, "mem\n", 4);
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
        return true;
    }
    return false;
}
