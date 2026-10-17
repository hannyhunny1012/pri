#pragma once

#include <cstdint>

namespace livestem
{

/** What a poll saw on disk. */
struct FileSignature
{
    bool exists = false;
    int64_t size = -1;
    int64_t modifiedNs = 0;

    bool operator== (const FileSignature& o) const noexcept { return exists == o.exists && size == o.size && modifiedNs == o.modifiedNs; }
    bool operator!= (const FileSignature& o) const noexcept { return ! operator== (o); }
};

/** Decides when a polled file should be (re)loaded.

    A file is loaded only after its size and modification time have stayed the same for
    settleMs, so a file that is still being written by the DAW or by Syncthing is not read.
    A signature that was already handled (loaded, or failed) is not loaded again until the
    file changes.
*/
class StabilityTracker
{
public:
    explicit StabilityTracker (int64_t settleMs = 500) noexcept : settle (settleMs) {}

    /** Feed one poll result. Returns true if the file should be loaded now. */
    bool observe (const FileSignature& seen, int64_t nowMs) noexcept
    {
        if (! hasSeen || seen != lastSeen)
        {
            hasSeen = true;
            lastSeen = seen;
            lastChangeMs = nowMs;
            return false;
        }

        if (! seen.exists || (hasHandled && seen == lastHandled))
            return false;

        return nowMs - lastChangeMs >= settle;
    }

    /** Call after a load attempt (successful or not) for the signature it read. */
    void markHandled (const FileSignature& handled) noexcept
    {
        hasHandled = true;
        lastHandled = handled;
    }

    bool wasHandled (const FileSignature& sig) const noexcept { return hasHandled && sig == lastHandled; }

    /** Forget everything, e.g. when the watched path changes. */
    void reset() noexcept
    {
        hasSeen = false;
        hasHandled = false;
    }

private:
    int64_t settle;
    bool hasSeen = false;
    bool hasHandled = false;
    FileSignature lastSeen, lastHandled;
    int64_t lastChangeMs = 0;
};

} // namespace livestem
