#pragma once

#include "livestem/AudioData.h"
#include "livestem/WavFile.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

namespace livestem::test
{

/** A fresh directory under the system temp dir, removed on destruction. */
struct TempDir
{
    TempDir()
    {
        static std::atomic<int> counter { 0 };
        path = std::filesystem::temp_directory_path()
             / ("livestem_test_" + std::to_string (std::random_device {}()) + "_" + std::to_string (counter++));
        std::filesystem::remove_all (path);
        std::filesystem::create_directories (path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (path, ec);
    }

    std::filesystem::path path;
};

/** Every sample of channel c at frame i is value + c * 0.25 (handy for checking mapping). */
inline AudioData makeConstant (int numChannels, int64_t numFrames, float value, double sampleRate = 48000.0)
{
    AudioData d;
    d.sampleRate = sampleRate;
    d.numFrames = numFrames;

    for (int c = 0; c < numChannels; ++c)
        d.channels.emplace_back ((size_t) numFrames, value + (float) c * 0.25f);

    return d;
}

/** Channel c, frame i holds (i % 1000) / 1000 + c. */
inline AudioData makeRamp (int numChannels, int64_t numFrames, double sampleRate = 48000.0)
{
    AudioData d;
    d.sampleRate = sampleRate;
    d.numFrames = numFrames;

    for (int c = 0; c < numChannels; ++c)
    {
        auto& ch = d.channels.emplace_back ((size_t) numFrames);

        for (int64_t i = 0; i < numFrames; ++i)
            ch[(size_t) i] = (float) (i % 1000) / 1000.0f + (float) c;
    }

    return d;
}

} // namespace livestem::test
