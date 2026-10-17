#include "LiveStemDocumentController.h"

#include "LiveStemPlaybackRenderer.h"

LiveStemDocumentController::LiveStemDocumentController (const ARA::PlugIn::PlugInEntry* entry,
                                                        const ARA::ARADocumentControllerHostInstance* instance)
    : juce::ARADocumentControllerSpecialisation (entry, instance),
      watcher (std::make_shared<livestem::StemWatcher> (500))
{
    watcher->start();
    startTimer (100);
}

LiveStemDocumentController::~LiveStemDocumentController()
{
    stopTimer();
    watcher->stop();
}

std::vector<LiveStemAudioSource*> LiveStemDocumentController::getAudioSources()
{
    if (auto* document = getDocumentController()->getDocument<juce::ARADocument>())
        return document->getAudioSources<LiveStemAudioSource>();

    return {};
}

void LiveStemDocumentController::setStemPath (LiveStemAudioSource& source, const juce::File& file)
{
    source.getStem().setPath (toStdPath (file.getFullPathName()));

    // The binding is part of the document state the host has to save.
    getDocumentController()->notifyDocumentDataChanged();
}

void LiveStemDocumentController::willBeginEditing (juce::ARADocument*)
{
    editGuard.beginEdit();
    isEditing = true;
}

void LiveStemDocumentController::didEndEditing (juce::ARADocument*)
{
    isEditing = false;
    editGuard.endEdit();
}

juce::ARAAudioSource* LiveStemDocumentController::doCreateAudioSource (juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef)
{
    return new LiveStemAudioSource (document, hostRef, watcher);
}

juce::ARAPlaybackRenderer* LiveStemDocumentController::doCreatePlaybackRenderer() noexcept
{
    return new LiveStemPlaybackRenderer (getDocumentController(), *this);
}

void LiveStemDocumentController::timerCallback()
{
    if (isEditing)
        return;

    for (auto* source : getAudioSources())
    {
        const auto version = source->getStem().getVersion();

        if (version != source->lastNotifiedVersion)
        {
            source->lastNotifiedVersion = version;
            notifyContentChanged (*source);
        }
    }
}

void LiveStemDocumentController::notifyContentChanged (LiveStemAudioSource& source)
{
    // The host's samples are untouched; what changes is what the plug-in renders for every
    // modification and playback region that uses this source.
    const auto scope = juce::ARAContentUpdateScopes::samplesAreAffected();

    for (auto* modification : source.getAudioModifications())
    {
        modification->notifyContentChanged (scope, true);

        for (auto* region : modification->getPlaybackRegions<juce::ARAPlaybackRegion>())
            region->notifyContentChanged (scope, true);
    }
}

//==============================================================================
// Archive layout (v1): int64 count, then per audio source: persistent ID, UTF-8 path.

bool LiveStemDocumentController::doStoreObjectsToStream (juce::ARAOutputStream& output, const juce::ARAStoreObjectsFilter* filter) noexcept
{
    const auto& sources = filter->getAudioSourcesToStore<LiveStemAudioSource>();

    if (! output.writeInt64 ((juce::int64) sources.size()))
        return false;

    for (const auto* source : sources)
    {
        if (! output.writeString (juce::String::fromUTF8 (source->getPersistentID().c_str())))
            return false;

        if (! output.writeString (fromStdPath (source->getStem().getPath())))
            return false;
    }

    return true;
}

bool LiveStemDocumentController::doRestoreObjectsFromStream (juce::ARAInputStream& input, const juce::ARARestoreObjectsFilter* filter) noexcept
{
    const auto count = input.readInt64();

    for (juce::int64 i = 0; i < count && ! input.failed(); ++i)
    {
        const auto persistentID = input.readString();
        const auto path = input.readString();

        if (auto* source = filter->getAudioSourceToRestoreStateWithID<LiveStemAudioSource> (persistentID.toRawUTF8()))
        {
            source->getStem().setPath (toStdPath (path));

            // Clearing the old buffer is part of the restore; the host is told once the file loads.
            source->lastNotifiedVersion = source->getStem().getVersion();
        }
    }

    return ! input.failed();
}
