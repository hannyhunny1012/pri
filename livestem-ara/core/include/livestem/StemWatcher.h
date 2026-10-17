#pragma once

#include "StemFile.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace livestem
{

/** Background thread that polls every registered StemFile and loads changed files. */
class StemWatcher
{
public:
    explicit StemWatcher (int intervalMs = 500) : interval (intervalMs) {}
    ~StemWatcher() { stop(); }

    StemWatcher (const StemWatcher&) = delete;
    StemWatcher& operator= (const StemWatcher&) = delete;

    void add (std::shared_ptr<StemFile> stem);
    void remove (const StemFile* stem);

    void start();
    void stop();

    /** Runs one polling pass on the calling thread (tests). */
    void pollOnce();

    static int64_t nowMs();

private:
    void run();

    const int interval;
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::shared_ptr<StemFile>> stems;
    std::thread thread;
    bool running = false;
};

} // namespace livestem
