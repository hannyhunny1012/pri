#pragma once

#include "AudioData.h"

#include <filesystem>
#include <memory>
#include <string>

namespace livestem
{

struct WavReadResult
{
    std::shared_ptr<AudioData> data;
    std::string error;

    bool ok() const noexcept { return data != nullptr; }
};

/** Reads a RIFF/WAVE file (PCM 8/16/24/32-bit int, IEEE float 32/64-bit, WAVE_FORMAT_EXTENSIBLE)
    into memory as 32-bit float. Never call this on the audio thread.
*/
WavReadResult readWavFile (const std::filesystem::path& file);

enum class WavSampleFormat
{
    int16,
    int24,
    float32
};

/** Writes a WAV file. Used by tests and tools; the plug-in itself never writes audio. */
bool writeWavFile (const std::filesystem::path& file, const AudioData& data, WavSampleFormat format);

} // namespace livestem
