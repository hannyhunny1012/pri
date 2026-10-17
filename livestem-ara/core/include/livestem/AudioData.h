#pragma once

#include <cstdint>
#include <vector>

namespace livestem
{

/** Decoded audio held in memory. Channels are stored non-interleaved as 32-bit float.
    Instances are immutable once published to the audio thread.
*/
struct AudioData
{
    double sampleRate = 0.0;
    int64_t numFrames = 0;
    std::vector<std::vector<float>> channels;

    int numChannels() const noexcept { return (int) channels.size(); }
    const float* channel (int index) const noexcept { return channels[(size_t) index].data(); }

    /** Approximate heap usage in bytes. */
    int64_t memoryBytes() const noexcept { return numFrames * (int64_t) channels.size() * (int64_t) sizeof (float); }
};

} // namespace livestem
