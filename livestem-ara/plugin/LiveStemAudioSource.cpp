#include "LiveStemAudioSource.h"

LiveStemAudioSource::LiveStemAudioSource (juce::ARADocument* document, ARA::ARAAudioSourceHostRef hostRef,
                                          std::shared_ptr<livestem::StemWatcher> w)
    : juce::ARAAudioSource (document, hostRef),
      watcher (std::move (w)),
      stem (std::make_shared<livestem::StemFile>())
{
    watcher->add (stem);
}

LiveStemAudioSource::~LiveStemAudioSource()
{
    watcher->remove (stem.get());
}

juce::String LiveStemAudioSource::getDisplayName() const
{
    const char* name = getName();

    if (name != nullptr && *name != 0)
        return juce::String::fromUTF8 (name);

    return juce::String::fromUTF8 (getPersistentID().c_str());
}
