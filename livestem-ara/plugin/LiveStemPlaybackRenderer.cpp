#include "LiveStemPlaybackRenderer.h"

#include "LiveStemDocumentController.h"
#include "livestem/RegionPlan.h"

LiveStemPlaybackRenderer::LiveStemPlaybackRenderer (ARA::PlugIn::DocumentController* dc, LiveStemDocumentController& c)
    : juce::ARAPlaybackRenderer (dc), controller (c)
{
}

void LiveStemPlaybackRenderer::prepareToPlay (double newSampleRate, int maximumSamplesPerBlock, int newNumChannels,
                                              juce::AudioProcessor::ProcessingPrecision, AlwaysNonRealtime alwaysNonRealtime)
{
    sampleRate = newSampleRate;
    maxBlockSize = maximumSamplesPerBlock;
    numChannels = newNumChannels;
    controller.setRenderSampleRate (sampleRate);

    fallbackBuffer.setSize (numChannels, maxBlockSize);
    hostReaders.clear();

    const bool buffered = alwaysNonRealtime == AlwaysNonRealtime::no;

    for (auto* region : getPlaybackRegions())
    {
        auto* source = region->getAudioModification()->getAudioSource();

        if (hostReaders.count (source) != 0)
            continue;

        auto direct = std::make_unique<juce::ARAAudioSourceReader> (source);
        HostReader entry;

        if (buffered)
        {
            const auto readAhead = juce::jmax (4 * maximumSamplesPerBlock, juce::roundToInt (2.0 * sampleRate));
            auto bufferingReader = std::make_unique<juce::BufferingAudioReader> (direct.release(), *readAheadThread, readAhead);
            entry.buffering = bufferingReader.get();
            entry.reader = std::move (bufferingReader);
        }
        else
        {
            entry.reader = std::move (direct);
        }

        hostReaders.emplace (source, std::move (entry));
    }
}

void LiveStemPlaybackRenderer::releaseResources()
{
    hostReaders.clear();
    fallbackBuffer.setSize (0, 0);
}

bool LiveStemPlaybackRenderer::processBlock (juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime,
                                             const juce::AudioPlayHead::PositionInfo& positionInfo) noexcept
{
    buffer.clear();

    const livestem::EditGuard::RenderScope renderScope (controller.getEditGuard());

    if (! renderScope.isEntered() || ! positionInfo.getIsPlaying())
        return true;

    const auto blockStart = positionInfo.getTimeInSamples().orFallback (0);
    const auto blockSize = buffer.getNumSamples();

    for (auto* region : getPlaybackRegions())
    {
        auto* source = region->getAudioModification()->getAudioSource<LiveStemAudioSource>();
        const auto timing = livestem::makeRegionTiming (region->getStartInPlaybackTime(),
                                                        region->getEndInPlaybackTime(),
                                                        region->getStartInAudioModificationTime(),
                                                        (double) source->getSampleCount() / source->getSampleRate(),
                                                        sampleRate);

        const auto stemBuffer = source->getStem().buffer().read();

        if (stemBuffer && juce::exactlyEqual (stemBuffer->sampleRate, sampleRate))
        {
            const auto plan = livestem::planRegionRead (blockStart, blockSize, timing, stemBuffer->numFrames);
            livestem::addFromBuffer (*stemBuffer.get(), plan, buffer.getArrayOfWritePointers(), buffer.getNumChannels());
        }
        else
        {
            // A region whose host audio is not ready stays silent rather than letting the
            // processor pass its input through for the whole block.
            renderFallback (*region, *source, blockStart, buffer, realtime);
        }
    }

    return true;
}

bool LiveStemPlaybackRenderer::renderFallback (juce::ARAPlaybackRegion& region, juce::ARAAudioSource& source, int64_t blockStart,
                                               juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime realtime) noexcept
{
    // Host audio can only be played without conversion.
    if (! juce::exactlyEqual (source.getSampleRate(), sampleRate))
        return false;

    const auto it = hostReaders.find (&source);

    if (it == hostReaders.end())
        return false;

    const auto timing = livestem::makeRegionTiming (region.getStartInPlaybackTime(), region.getEndInPlaybackTime(),
                                                    region.getStartInAudioModificationTime(),
                                                    (double) source.getSampleCount() / source.getSampleRate(), sampleRate);
    const auto plan = livestem::planRegionRead (blockStart, buffer.getNumSamples(), timing, source.getSampleCount());

    if (plan.numFromBuffer <= 0)
        return true;

    if (it->second.buffering != nullptr)
        it->second.buffering->setReadTimeout (realtime == juce::AudioProcessor::Realtime::no ? 100 : 0);

    const auto sourceChannels = (int) it->second.reader->numChannels;

    if (! it->second.reader->read (&fallbackBuffer, 0, plan.numFromBuffer, plan.readStart, true, sourceChannels > 1))
        return false;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.addFrom (ch, plan.outputStart, fallbackBuffer, juce::jmin (ch, juce::jmin (sourceChannels, fallbackBuffer.getNumChannels()) - 1), 0, plan.numFromBuffer);

    return true;
}
