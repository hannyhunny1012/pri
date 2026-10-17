#pragma once

#include "AudioData.h"
#include "SharedSlot.h"
#include "StabilityTracker.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace livestem
{

enum class StemStatus
{
    unassigned, ///< no file chosen
    waiting,    ///< file seen, waiting for it to stop changing
    loading,    ///< reading in the background
    ready,      ///< latest file content is loaded
    error,      ///< last load failed; the previous content (if any) keeps playing
    missing     ///< file not found
};

const char* statusName (StemStatus status) noexcept;

struct StemInfo
{
    StemStatus status = StemStatus::unassigned;
    std::filesystem::path path;
    std::string message;           ///< error or warning text, empty if none
    uint64_t version = 0;          ///< increments whenever the playable buffer changes
    int64_t loadedAtUnixMs = 0;    ///< wall-clock time of the last successful load
    double sampleRate = 0.0;       ///< of the loaded buffer
    int numChannels = 0;
    int64_t numFrames = 0;
};

/** One watched shared WAV and the buffer the audio thread plays from.

    setPath()/getInfo() may be called from any non-audio thread, poll() only from the watcher
    thread, and buffer().read() from the audio thread.
*/
class StemFile
{
public:
    explicit StemFile (int64_t settleMs = 500) : tracker (settleMs) {}

    void setPath (const std::filesystem::path& newPath);
    std::filesystem::path getPath() const;

    StemInfo getInfo() const;
    uint64_t getVersion() const noexcept { return version.load(); }

    const SharedSlot<AudioData>& buffer() const noexcept { return slot; }

    /** Checks the file once and loads it if it changed and has settled. Watcher thread only. */
    void poll (int64_t nowMs);

    static FileSignature signatureOf (const std::filesystem::path& file);

private:
    void setStatus (StemStatus status, std::string message);

    mutable std::mutex mutex;
    std::filesystem::path path;
    uint64_t pathGeneration = 0;
    StemInfo info;

    // Watcher thread only
    StabilityTracker tracker;
    uint64_t trackedGeneration = ~(uint64_t) 0;
    bool lastLoadSucceeded = false;

    SharedSlot<AudioData> slot;
    std::atomic<uint64_t> version { 0 };
};

} // namespace livestem
