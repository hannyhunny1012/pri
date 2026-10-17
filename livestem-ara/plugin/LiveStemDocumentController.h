#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LiveStemAudioSource.h"
#include "livestem/EditGuard.h"
#include "livestem/StemWatcher.h"

#include <atomic>

/** One per ARA document (shared by every LiveStem instance in the project). */
class LiveStemDocumentController final : public juce::ARADocumentControllerSpecialisation,
                                         private juce::Timer
{
public:
    LiveStemDocumentController (const ARA::PlugIn::PlugInEntry* entry,
                                const ARA::ARADocumentControllerHostInstance* instance);
    ~LiveStemDocumentController() override;

    /** Message thread. Binds an audio source to a shared WAV (empty path unbinds). */
    void setStemPath (LiveStemAudioSource& source, const juce::File& file);

    std::vector<LiveStemAudioSource*> getAudioSources();

    /** Sample rate of the most recent prepareToPlay, 0 if never prepared. */
    double getRenderSampleRate() const noexcept { return renderSampleRate.load(); }
    void setRenderSampleRate (double rate) noexcept { renderSampleRate.store (rate); }

    livestem::EditGuard& getEditGuard() noexcept { return editGuard; }

protected:
    void willBeginEditing (juce::ARADocument*) override;
    void didEndEditing (juce::ARADocument*) override;

    juce::ARAAudioSource* doCreateAudioSource (juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef) override;
    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() noexcept override;

    bool doRestoreObjectsFromStream (juce::ARAInputStream& input, const juce::ARARestoreObjectsFilter* filter) noexcept override;
    bool doStoreObjectsToStream (juce::ARAOutputStream& output, const juce::ARAStoreObjectsFilter* filter) noexcept override;

private:
    void timerCallback() override;
    void notifyContentChanged (LiveStemAudioSource& source);

    std::shared_ptr<livestem::StemWatcher> watcher;
    livestem::EditGuard editGuard;
    std::atomic<double> renderSampleRate { 0.0 };
    bool isEditing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveStemDocumentController)
};
