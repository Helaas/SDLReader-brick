#include "mupdf_locking.h"

#include <array>
#include <mutex>

namespace
{
struct SharedMuPdfLockState
{
    std::array<std::mutex, FZ_LOCK_MAX> mutexes;

    SharedMuPdfLockState(const SharedMuPdfLockState&) = delete;
    SharedMuPdfLockState& operator=(const SharedMuPdfLockState&) = delete;
    SharedMuPdfLockState() = default;
    ~SharedMuPdfLockState() = default;
};

void lockCallback(void* user, int lockIndex)
{
    if (!user)
    {
        return;
    }

    auto* state = static_cast<SharedMuPdfLockState*>(user);
    if (lockIndex >= 0 && lockIndex < FZ_LOCK_MAX)
    {
        state->mutexes[static_cast<size_t>(lockIndex)].lock();
    }
}

void unlockCallback(void* user, int lockIndex)
{
    if (!user)
    {
        return;
    }

    auto* state = static_cast<SharedMuPdfLockState*>(user);
    if (lockIndex >= 0 && lockIndex < FZ_LOCK_MAX)
    {
        state->mutexes[static_cast<size_t>(lockIndex)].unlock();
    }
}
} // namespace

const fz_locks_context* getSharedMuPdfLocks()
{
    static SharedMuPdfLockState lockState;
    static const fz_locks_context locksContext{&lockState, lockCallback, unlockCallback};
    return &locksContext;
}
