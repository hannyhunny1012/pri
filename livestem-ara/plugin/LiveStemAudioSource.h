#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "livestem/StemFile.h"
#include "livestem/StemWatcher.h"

/** UTF-8 safe conversions (std::filesystem::path(std::string) uses the ANSI code page on Windows). */
inline std::filesystem::path toStdPath (const juce::String& path)
{
    return path.isEmpty() ? std::filesystem::path() : std::filesystem::u8path (path.toStdString());
}

inline juce::String fromStdPath (const std::filesystem::path& path)
{
    return juce::String::fromUTF8 (path.u8string().c_str());
}

/** ARA audio source that carries the shared WAV it is bound to.
    The audio thread reaches the stem buffer directly through this object (no lookups).
*/
class LiveStemAudioSource final : public juce::ARAAudioSource
{
public:
    LiveStemAudioSource (juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef,
                         std::shared_ptr<livestem::StemWatcher> watcher);
    ~LiveStemAudioSource() override;

    livestem::StemFile& getStem() noexcept { return *stem; }
    const livestem::StemFile& getStem() const noexcept { return *stem; }

    juce::String getDisplayName() const;

    /** Message thread only: last stem version the host was told about. */
    uint64_t lastNotifiedVersion = 0;

private:
    std::shared_ptr<livestem::StemWatcher> watcher; // shared so it outlives sources destroyed after the controller
    std::shared_ptr<livestem::StemFile> stem;
};
