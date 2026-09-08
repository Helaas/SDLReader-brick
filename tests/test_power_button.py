#!/usr/bin/env python3
"""Run the real event loop/handler with a fake clock and inert sleep/shutdown.

Requires a C++17 compiler. No input devices or power-management commands are used.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'ports/tg5040/src/power_handler.cpp').read_text()
start = source.index('void PowerHandler::threadMain()')
end = source.index('void PowerHandler::attemptSleep()', start)
methods = source[start:end].replace('std::chrono::steady_clock', 'TestClock')
methods = methods.replace('std::this_thread::sleep_for', 'ignoreDelay')
harness = r'''
#include <atomic>
#include <chrono>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <unistd.h>
#define DEBUG_LOG(x) do {} while (0)
#define DEBUG_ERR(x) do {} while (0)
struct TestClock {
    using time_point = std::chrono::steady_clock::time_point;
    static inline time_point current;
    static time_point now() { return current; }
};
struct input_event { int type, code, value; };
constexpr int EV_KEY = 1;
bool isMy355Platform() { return true; }
template<class T> void ignoreDelay(T) {}
int reads, releaseAt, repeatAt;
ssize_t fakeRead(int, void* buffer, size_t size) {
    ++reads;
    assert(reads < 100); // Fail if neither sleep nor shutdown was requested.
    TestClock::current += std::chrono::milliseconds(50);
    auto* event = static_cast<input_event*>(buffer);
    if (reads == 1 || reads == releaseAt || reads == repeatAt) {
        *event = {EV_KEY, 116, reads == 1 ? 1 : (reads == releaseAt ? 0 : 2)};
        return size;
    }
    errno = EAGAIN;
    return -1;
}
#define read fakeRead
class PowerHandler {
public:
    std::atomic<bool> m_running{true}, m_in_fake_sleep{false};
    int m_device_fd = -1, sleeps = 0, shutdowns = 0;
    TestClock::time_point m_resume_ignore_until{};
    static constexpr int POWER_KEY_CODE = 116;
    static constexpr auto SHORT_PRESS_MAX = std::chrono::milliseconds(2000);
    void threadMain();
    void handlePowerButtonEvent(const input_event&, TestClock::time_point&);
    void requestShutdown() { ++shutdowns; m_running = false; }
    void attemptSleep() { ++sleeps; m_running = false; }
    void exitFakeSleep() { m_in_fake_sleep = false; }
    void tryDeepSleep() {}
    bool reopenDevice() { assert(false); return false; }
};
'''
checks = r'''
int main() {
    for (int release : {0, 5, 41}) {
        for (int repeat : {0, 20}) {
            reads = 0; releaseAt = release; repeatAt = repeat;
            TestClock::current = TestClock::time_point{} + std::chrono::seconds(1);
            PowerHandler handler;
            handler.threadMain();
            assert(handler.sleeps == (release == 5 ? 1 : 0));
            assert(handler.shutdowns == (release == 5 ? 0 : 1));
        }
    }
    std::cout << "Short press and held/released long presses, with/without repeats: passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='sdlreader-power-test-') as directory:
    cpp = Path(directory) / 'check.cpp'
    executable = Path(directory) / 'check'
    cpp.write_text(harness + methods + checks)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
