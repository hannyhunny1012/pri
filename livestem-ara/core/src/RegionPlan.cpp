#include "livestem/RegionPlan.h"

#include <algorithm>
#include <cmath>

namespace livestem
{

namespace
{
int64_t roundSamplePosition (double seconds, double sampleRate) noexcept
{
    return (int64_t) std::floor (seconds * sampleRate + 0.5);
}
} // namespace

RegionTiming makeRegionTiming (double playbackStartSeconds, double playbackEndSeconds,
                               double sourceStartSeconds, double sourceDurationSeconds,
                               double sampleRate) noexcept
{
    RegionTiming t;
    t.playbackStart = roundSamplePosition (playbackStartSeconds, sampleRate);
    t.playbackEnd = roundSamplePosition (playbackEndSeconds, sampleRate);
    t.sourceStart = roundSamplePosition (sourceStartSeconds, sampleRate);
    t.sourceLength = roundSamplePosition (sourceDurationSeconds, sampleRate);
    return t;
}

RegionReadPlan planRegionRead (int64_t blockStart, int blockSize,
                               const RegionTiming& region, int64_t bufferLength) noexcept
{
    RegionReadPlan plan;

    // Timeline position where source sample 0 would play.
    const auto sourceZeroOnTimeline = region.playbackStart - region.sourceStart;

    const auto start = std::max ({ blockStart, region.playbackStart, sourceZeroOnTimeline });
    const auto end = std::min ({ blockStart + (int64_t) blockSize, region.playbackEnd, sourceZeroOnTimeline + region.sourceLength });

    if (end <= start)
        return plan;

    plan.outputStart = (int) (start - blockStart);
    plan.numSamples = (int) (end - start);
    plan.readStart = start - sourceZeroOnTimeline;
    plan.numFromBuffer = (int) std::clamp (bufferLength - plan.readStart, (int64_t) 0, (int64_t) plan.numSamples);
    return plan;
}

void addFromBuffer (const AudioData& data, const RegionReadPlan& plan,
                    float* const* output, int numOutputChannels) noexcept
{
    const int numSource = data.numChannels();

    if (plan.numFromBuffer <= 0 || numSource == 0 || numOutputChannels <= 0)
        return;

    const auto n = plan.numFromBuffer;

    if (numOutputChannels == 1 && numSource > 1)
    {
        const float gain = 1.0f / (float) numSource;
        auto* dest = output[0] + plan.outputStart;

        for (int ch = 0; ch < numSource; ++ch)
        {
            const auto* src = data.channel (ch) + plan.readStart;

            for (int i = 0; i < n; ++i)
                dest[i] += src[i] * gain;
        }

        return;
    }

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        const auto* src = data.channel (std::min (ch, numSource - 1)) + plan.readStart;
        auto* dest = output[ch] + plan.outputStart;

        for (int i = 0; i < n; ++i)
            dest[i] += src[i];
    }
}

} // namespace livestem
