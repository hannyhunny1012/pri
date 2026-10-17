#include "livestem/StemWatcher.h"

#include <algorithm>
#include <chrono>

namespace livestem
{

int64_t StemWatcher::nowMs()
{
    return (int64_t) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

void StemWatcher::add (std::shared_ptr<StemFile> stem)
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        stems.push_back (std::move (stem));
    }

    wake.notify_all();
}

void StemWatcher::remove (const StemFile* stem)
{
    const std::lock_guard<std::mutex> lock (mutex);
    stems.erase (std::remove_if (stems.begin(), stems.end(), [stem] (const auto& s) { return s.get() == stem; }), stems.end());
}

void StemWatcher::start()
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (running)
        return;

    running = true;
    thread = std::thread ([this] { run(); });
}

void StemWatcher::stop()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (! running)
            return;

        running = false;
    }

    wake.notify_all();
    thread.join();
}

void StemWatcher::pollOnce()
{
    std::vector<std::shared_ptr<StemFile>> snapshot;

    {
        const std::lock_guard<std::mutex> lock (mutex);
        snapshot = stems;
    }

    for (auto& stem : snapshot)
        stem->poll (nowMs());
}

void StemWatcher::run()
{
    for (;;)
    {
        pollOnce();

        std::unique_lock<std::mutex> lock (mutex);
        wake.wait_for (lock, std::chrono::milliseconds (interval), [this] { return ! running; });

        if (! running)
            return;
    }
}

} // namespace livestem
