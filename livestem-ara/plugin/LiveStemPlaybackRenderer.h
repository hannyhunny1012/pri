#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>

class LiveStemDocumentController;

/** Renders each playback region from the latest shared WAV, falling back to the host's audio
    when no file is loaded (or its sample rate does not match the project in phase 1).
*/
class LiveStemPlaybackRenderer final : public juce::ARAPlaybackRenderer
{
public:
    LiveStemPlaybackRenderer (ARA::PlugIn::DocumentController* dc, LiveStemDocumentController& controller);

    void prepareToPlay (double sampleRate, int maximumSamplesPerBlock, int numChannels,
                        juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime alwaysNonRealtime) override;
    void releaseResources() override;

    bool processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                       const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept override;

    using juce::ARAPlaybackRenderer::processBlock;

private:
    bool renderFallback (juce::ARAPlaybackRegion& region, juce::ARAAudioSource& source, int64_t blockStart,
                         juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime) noexcept;

    struct ReadAheadThread final : juce::TimeSliceThread
    {
        ReadAheadThread() : juce::TimeSliceThread ("LiveStem host audio read-ahead") { startThread (Priority::high); }
    };

    struct HostReader
    {
        std::unique_ptr<juce::AudioFormatReader> reader;
        juce::BufferingAudioReader* buffering = nullptr; // same object as reader when buffered
    };

    LiveStemDocumentController& controller;
    juce::SharedResourcePointer<ReadAheadThread> readAheadThread;

    // Host audio, used only while the shared file is unavailable.
    std::map<juce::ARAAudioSource*, HostReader> hostReaders;

    juce::AudioBuffer<float> fallbackBuffer;
    double sampleRate = 48000.0;
    int maxBlockSize = 0;
    int numChannels = 2;
};
