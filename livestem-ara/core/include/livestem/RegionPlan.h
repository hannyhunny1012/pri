#pragma once

#include "AudioData.h"

#include <cstdint>

namespace livestem
{

/** One playback region expressed in samples at the render sample rate.
    No time stretching: one timeline sample corresponds to one source sample.
*/
struct RegionTiming
{
    int64_t playbackStart = 0; ///< region start on the timeline
    int64_t playbackEnd = 0;   ///< region end on the timeline (exclusive)
    int64_t sourceStart = 0;   ///< source position heard at playbackStart (may be negative)
    int64_t sourceLength = 0;  ///< length of the source the host knows about (the originally imported file)
};

/** What a region contributes to one processing block. */
struct RegionReadPlan
{
    int outputStart = 0;     ///< first block sample written by this region
    int numSamples = 0;      ///< samples covered by the region inside the block
    int64_t readStart = 0;   ///< buffer position that maps to outputStart
    int numFromBuffer = 0;   ///< leading samples read from the buffer; the rest are silent

    bool isEmpty() const noexcept { return numSamples <= 0; }
};

/** Builds a RegionTiming from ARA times (seconds). Rounds like ARA::roundSamplePosition. */
RegionTiming makeRegionTiming (double playbackStartSeconds, double playbackEndSeconds,
                               double sourceStartSeconds, double sourceDurationSeconds,
                               double sampleRate) noexcept;

/** Maps a processing block onto a region and a buffer of bufferLength samples.

    - Only the part of the block inside the region and inside [0, sourceLength) of the source is covered.
      Anything past the original source length is never used, even if the buffer is longer.
    - If the buffer is shorter than the requested source range, the remainder is silence.
*/
RegionReadPlan planRegionRead (int64_t blockStart, int blockSize,
                               const RegionTiming& region, int64_t bufferLength) noexcept;

/** Adds the buffer part of a plan into the output channels. Real-time safe.

    Channel mapping: a mono buffer goes to every output channel, a multi-channel buffer into a mono
    output is averaged, otherwise output channel c takes buffer channel min(c, last).
*/
void addFromBuffer (const AudioData& data, const RegionReadPlan& plan,
                    float* const* output, int numOutputChannels) noexcept;

} // namespace livestem
