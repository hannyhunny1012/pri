// A minimal ARA host for testing LiveStem ARA without a DAW.
//
// Usage: livestem_ara_hosttest "<path to LiveStem ARA.vst3>"
//
// It loads the plug-in in ARA mode, creates a document with one audio source (constant 0.1 in the
// host's own samples) and several playback regions, then checks the rendered output while the shared
// WAV is bound, rewritten, shortened, lengthened, deleted and stored/restored through the ARA archive.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_events/juce_events.h>

#include <ARA_Library/Dispatch/ARAHostDispatch.h>

#include "livestem/WavFile.h"

using namespace juce;

namespace
{
constexpr double rate = 48000.0;
constexpr int blockSize = 512;
constexpr int64 sourceFrames = (int64) (2.0 * rate); // the clip the "DAW" imported: 2 seconds
constexpr float hostValue = 0.1f;                     // what the host's own samples contain

int failures = 0;

void check (bool ok, const String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;

    if (! ok)
        ++failures;
}

void pump (int ms) { MessageManager::getInstance()->runDispatchLoopUntil (ms); }

//==============================================================================
// Host side interfaces

struct HostSource
{
    String name = "bob_bass.wav";
    String persistentID = "source-1";
};

HostSource hostSource;

class AudioAccess final : public ARA::Host::AudioAccessControllerInterface
{
public:
    ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef, bool use64) noexcept override
    {
        return reinterpret_cast<ARA::ARAAudioReaderHostRef> (use64 ? 2 : 1);
    }

    bool readAudioSamples (ARA::ARAAudioReaderHostRef reader, ARA::ARASamplePosition pos, ARA::ARASampleCount n, void* const* buffers) noexcept override
    {
        if (reinterpret_cast<intptr_t> (reader) != 1)
            return false;

        for (int ch = 0; ch < 2; ++ch)
            for (ARA::ARASampleCount i = 0; i < n; ++i)
                static_cast<float*> (buffers[ch])[i] = (pos + i >= 0 && pos + i < sourceFrames) ? hostValue : 0.0f;

        return true;
    }

    void destroyAudioReader (ARA::ARAAudioReaderHostRef) noexcept override {}
};

class Archiving final : public ARA::Host::ArchivingControllerInterface
{
public:
    ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef r) noexcept override { return (ARA::ARASize) block (r).getSize(); }

    bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef r, ARA::ARASize pos, ARA::ARASize len, ARA::ARAByte* buffer) noexcept override
    {
        if (pos + len > block (r).getSize())
            return false;

        std::memcpy (buffer, addBytesToPointer (block (r).getData(), pos), len);
        return true;
    }

    bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef w, ARA::ARASize pos, ARA::ARASize len, const ARA::ARAByte* buffer) noexcept override
    {
        auto* out = reinterpret_cast<MemoryOutputStream*> (w);
        return out->setPosition ((int64) pos) && out->write (buffer, len);
    }

    void notifyDocumentArchivingProgress (float) noexcept override {}
    void notifyDocumentUnarchivingProgress (float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef) noexcept override { return "com.livestem.ara.archive.v1"; }

private:
    static MemoryBlock& block (ARA::ARAArchiveReaderHostRef r) { return *reinterpret_cast<MemoryBlock*> (r); }
};

std::atomic<int> regionContentChanges { 0 }, modificationContentChanges { 0 }, documentDataChanges { 0 };

class ModelUpdates final : public ARA::Host::ModelUpdateControllerInterface
{
public:
    void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef, ARA::ARAAnalysisProgressState, float) noexcept override {}
    void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override { ++modificationContentChanges; }
    void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override { ++regionContentChanges; }
    void notifyDocumentDataChanged() noexcept override { ++documentDataChanges; }
};

struct PlayHead final : AudioPlayHead
{
    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setTimeInSamples (position);
        info.setTimeInSeconds ((double) position / rate);
        info.setIsPlaying (true);
        return info;
    }

    int64 position = 0;
};

//==============================================================================
/** One "project": a document controller, a plug-in instance and the ARA model graph. */
struct Project
{
    explicit Project (const PluginDescription& description)
    {
        AudioPluginFormatManager manager;
        addHeadlessDefaultFormatsToManager (manager);
        String error;
        plugin = manager.createPluginInstance (description, rate, blockSize, error);

        if (plugin == nullptr)
        {
            std::cout << "Could not load plug-in: " << error << std::endl;
            return;
        }

        plugin->setPlayHead (&playHead);

        bool done = false;
        createARAFactoryAsync (*plugin, [&] (ARAFactoryWrapper factory)
        {
            if (factory.get() != nullptr)
                controller = ARAHostDocumentController::create (std::move (factory), "LiveStem host test",
                                                                std::make_unique<AudioAccess>(), std::make_unique<Archiving>(),
                                                                nullptr, std::make_unique<ModelUpdates>(), nullptr);
            done = true;
        });

        for (int i = 0; i < 100 && ! done; ++i)
            pump (20);

        if (controller == nullptr)
            return;

        auto& dc = controller->getDocumentController();

        {
            const ARAEditGuard guard (dc);

            auto contextProps = ARAHostModel::MusicalContext::getEmptyProperties();
            contextProps.name = "context";
            context = std::make_unique<ARAHostModel::MusicalContext> (reinterpret_cast<ARA::ARAMusicalContextHostRef> (1), dc, contextProps);

            auto sequenceProps = ARAHostModel::RegionSequence::getEmptyProperties();
            sequenceProps.name = "track";
            sequenceProps.musicalContextRef = context->getPluginRef();
            sequence = std::make_unique<ARAHostModel::RegionSequence> (reinterpret_cast<ARA::ARARegionSequenceHostRef> (1), dc, sequenceProps);

            auto sourceProps = ARAHostModel::AudioSource::getEmptyProperties();
            sourceProps.name = hostSource.name.toRawUTF8();
            sourceProps.persistentID = hostSource.persistentID.toRawUTF8();
            sourceProps.sampleCount = sourceFrames;
            sourceProps.sampleRate = rate;
            sourceProps.channelCount = 2;
            source = std::make_unique<ARAHostModel::AudioSource> (reinterpret_cast<ARA::ARAAudioSourceHostRef> (&hostSource), dc, sourceProps);
            source->enableAudioSourceSamplesAccess (true);

            auto modificationProps = ARAHostModel::AudioModification::getEmptyProperties();
            modificationProps.persistentID = "modification-1";
            modification = std::make_unique<ARAHostModel::AudioModification> (reinterpret_cast<ARA::ARAAudioModificationHostRef> (1), dc, *source, modificationProps);

            // Region A: the whole clip at 0 s. Region B: a duplicate of the first second, placed at 3 s.
            regionA = makeRegion (1, 0.0, 0.0, 2.0);
            regionB = makeRegion (2, 3.0, 0.0, 1.0);
        }

        const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
        renderer = controller->bindDocumentToPluginInstance (*plugin, roles, ARA::kARAPlaybackRendererRole).getPlaybackRendererInterface();
        renderer.add (*regionA);
        renderer.add (*regionB);

        plugin->prepareToPlay (rate, blockSize);
        valid = true;
    }

    ~Project()
    {
        if (plugin != nullptr)
            plugin->releaseResources();

        regionA.reset();
        regionB.reset();
        renderer = {};
        modification.reset();
        source.reset();
        sequence.reset();
        context.reset();
        controller.reset();
        plugin.reset();
    }

    ARA::ARAPlaybackRegionProperties regionProps (double playbackStart, double sourceStart, double duration) const
    {
        auto props = ARAHostModel::PlaybackRegion::getEmptyProperties();
        props.transformationFlags = ARA::kARAPlaybackTransformationNoChanges;
        props.startInModificationTime = sourceStart;
        props.durationInModificationTime = duration;
        props.startInPlaybackTime = playbackStart;
        props.durationInPlaybackTime = duration;
        props.musicalContextRef = context->getPluginRef();
        props.regionSequenceRef = sequence->getPluginRef();
        return props;
    }

    std::unique_ptr<ARAHostModel::PlaybackRegion> makeRegion (intptr_t ref, double playbackStart, double sourceStart, double duration)
    {
        return std::make_unique<ARAHostModel::PlaybackRegion> (reinterpret_cast<ARA::ARAPlaybackRegionHostRef> (ref),
                                                               controller->getDocumentController(), *modification,
                                                               regionProps (playbackStart, sourceStart, duration));
    }

    /** Renders [start, start + numSamples) of the timeline. */
    AudioBuffer<float> render (int64 start, int64 numSamples, bool offline = false)
    {
        plugin->setNonRealtime (offline);
        AudioBuffer<float> out (2, (int) numSamples);
        AudioBuffer<float> block (2, blockSize);
        MidiBuffer midi;

        for (int64 pos = 0; pos < numSamples; pos += blockSize)
        {
            const auto n = (int) std::min<int64> (blockSize, numSamples - pos);
            block.setSize (2, n, false, false, true);
            block.clear();
            playHead.position = start + pos;
            plugin->processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, (int) pos, block, ch, 0, n);
        }

        return out;
    }

    /** Value at a timeline second (left channel), rendered in isolation. */
    float valueAt (double seconds, bool offline = false)
    {
        const auto pos = (int64) (seconds * rate);
        const auto start = pos - pos % blockSize;
        return render (start, blockSize, offline).getSample (0, (int) (pos - start));
    }

    void restoreArchive (MemoryBlock& archive)
    {
        plugin->releaseResources();
        {
            const ARAEditGuard guard (controller->getDocumentController());
            controller->getDocumentController().restoreObjectsFromArchive (reinterpret_cast<ARA::ARAArchiveReaderHostRef> (&archive), nullptr);
        }
        plugin->prepareToPlay (rate, blockSize);
    }

    MemoryBlock storeArchive()
    {
        MemoryOutputStream out;
        controller->getDocumentController().storeObjectsToArchive (reinterpret_cast<ARA::ARAArchiveWriterHostRef> (&out), nullptr);
        return out.getMemoryBlock();
    }

    /** Lets the plug-in's watcher and timer run, and polls model updates like a DAW does. */
    void idle (int ms)
    {
        for (int elapsed = 0; elapsed < ms; elapsed += 50)
        {
            pump (50);
            controller->getDocumentController().notifyModelUpdates();
        }
    }

    /** Waits until valueAt(seconds) == expected, up to timeoutMs. Returns the time it took or -1. */
    int waitForValue (double seconds, float expected, int timeoutMs = 4000)
    {
        for (int elapsed = 0; elapsed <= timeoutMs; elapsed += 50)
        {
            if (std::abs (valueAt (seconds) - expected) < 1e-5f)
                return elapsed;

            idle (50);
        }

        return -1;
    }

    bool valid = false;
    PlayHead playHead;
    std::unique_ptr<AudioPluginInstance> plugin;
    std::unique_ptr<ARAHostDocumentController> controller;
    std::unique_ptr<ARAHostModel::MusicalContext> context;
    std::unique_ptr<ARAHostModel::RegionSequence> sequence;
    std::unique_ptr<ARAHostModel::AudioSource> source;
    std::unique_ptr<ARAHostModel::AudioModification> modification;
    std::unique_ptr<ARAHostModel::PlaybackRegion> regionA, regionB;
    ARAHostModel::PlaybackRendererInterface renderer;
};

/** Writes a stereo WAV. Left channel: value at every sample, or a ramp (frame / rate seconds) if value < 0. */
void writeStem (const File& file, double seconds, float value, double sampleRate = rate)
{
    livestem::AudioData d;
    d.sampleRate = sampleRate;
    d.numFrames = (int64) (seconds * sampleRate);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& c = d.channels.emplace_back ((size_t) d.numFrames);

        for (int64 i = 0; i < d.numFrames; ++i)
            c[(size_t) i] = value >= 0.0f ? value : (float) ((double) i / sampleRate) * 0.1f;
    }

    // Write next to the target and rename, the way Syncthing replaces files.
    const auto tmp = file.getSiblingFile (".tmp_" + file.getFileName());
    livestem::writeWavFile (std::filesystem::u8path (tmp.getFullPathName().toStdString()), d, livestem::WavSampleFormat::float32);

    // On Windows the move fails while the plug-in has the file open; retry like Syncthing does.
    for (int attempt = 0; attempt < 200 && ! tmp.moveFileTo (file); ++attempt)
        Thread::sleep (5);
}

MemoryBlock makeBindingArchive (const String& persistentID, const File& file)
{
    MemoryOutputStream out;
    out.writeInt64 (1);
    out.writeString (persistentID);
    out.writeString (file.getFullPathName());
    return out.getMemoryBlock();
}

bool archiveContains (const MemoryBlock& archive, const String& text)
{
    const auto* begin = static_cast<const char*> (archive.getData());
    const auto* end = begin + archive.getSize();
    const auto* needle = text.toRawUTF8();
    return std::search (begin, end, needle, needle + text.getNumBytesAsUTF8()) != end;
}

int runTests (const File& pluginFile)
{
    AudioPluginFormatManager manager;
    addHeadlessDefaultFormatsToManager (manager);
    OwnedArray<PluginDescription> types;

    for (auto* format : manager.getFormats())
        if (format->fileMightContainThisPluginType (pluginFile.getFullPathName()))
            format->findAllTypesForFile (types, pluginFile.getFullPathName());

    if (types.isEmpty())
    {
        std::cout << "No plug-in found in " << pluginFile.getFullPathName() << std::endl;
        return 2;
    }

    std::cout << "Plug-in: " << types[0]->name << " (" << types[0]->pluginFormatName << "), ARA: " << (types[0]->hasARAExtension ? "yes" : "no") << std::endl;
    check (types[0]->hasARAExtension, "plug-in exposes the ARA extension");

    const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("livestem_hosttest_" + String (Random::getSystemRandom().nextInt (1000000)));
    dir.createDirectory();
    const auto stem = dir.getChildFile (CharPointer_UTF8 ("bob_bass_\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.wav")); // non-ASCII path

    MemoryBlock storedArchive;

    {
        std::cout << "\n[1] Unbound: host audio plays (fallback)" << std::endl;
        Project p (*types[0]);
        check (p.valid, "ARA document controller created and plug-in bound as playback renderer");

        if (! p.valid)
            return 1;

        // Host audio is read ahead in the background, so allow it a moment like a DAW's pre-roll.
        check (p.waitForValue (0.5, hostValue) >= 0, "region A plays host samples");
        check (p.waitForValue (3.5, hostValue) >= 0, "region B (duplicate) plays host samples");
        check (std::abs (p.valueAt (0.5, true) - hostValue) < 1e-6f, "offline render of host samples");
        check (std::abs (p.valueAt (2.5)) < 1e-9f, "gap between regions is silent");

        std::cout << "\n[2] Bind the shared WAV (via archive restore) and load it" << std::endl;
        writeStem (stem, 2.0, 0.5f);
        auto archive = makeBindingArchive (hostSource.persistentID, stem);
        p.restoreArchive (archive);
        const auto loadMs = p.waitForValue (0.5, 0.5f);
        check (loadMs >= 0, "file content plays after binding (" + String (loadMs) + " ms)");
        check (std::abs (p.valueAt (3.5) - 0.5f) < 1e-6f, "duplicate region also plays file content");

        std::cout << "\n[3] Rewrite the file while 'playing'" << std::endl;
        const auto regionNotificationsBefore = regionContentChanges.load();
        writeStem (stem, 2.0, 0.8f);
        const auto swapMs = p.waitForValue (0.5, 0.8f);
        check (swapMs >= 0, "new content plays without reopening (" + String (swapMs) + " ms after write)");
        p.idle (300);
        check (regionContentChanges.load() > regionNotificationsBefore, "host received playback region content-changed notifications ("
                                                                           + String (regionContentChanges.load() - regionNotificationsBefore) + ")");
        check (modificationContentChanges.load() > 0, "host received audio modification content-changed notifications");

        std::cout << "\n[4] Region edits map onto the latest file" << std::endl;
        writeStem (stem, 2.0, -1.0f); // ramp: value = 0.1 * seconds into the source
        check (p.waitForValue (1.0, 0.1f) >= 0, "ramp file loaded");
        check (std::abs (p.valueAt (1.5) - 0.15f) < 1e-4f, "region A at 1.5 s reads source 1.5 s");
        check (std::abs (p.valueAt (3.5) - 0.05f) < 1e-4f, "duplicate at 3.5 s reads source 0.5 s");

        // Trim + move region A while prepared (a DAW edit during playback): now plays source 0.5..1.0 at 5.0 s.
        p.regionA->update (p.regionProps (5.0, 0.5, 0.5));
        check (std::abs (p.valueAt (0.7)) < 1e-9f, "old position of moved region is silent");
        check (std::abs (p.valueAt (5.25) - 0.075f) < 1e-4f, "moved+trimmed region reads source 0.75 s at 5.25 s");
        check (std::abs (p.valueAt (5.6)) < 1e-9f, "trimmed tail is silent");

        std::cout << "\n[5] Offline (bounce) rendering" << std::endl;
        check (std::abs (p.valueAt (5.25, true) - 0.075f) < 1e-4f, "offline render matches realtime");
        check (std::abs (p.valueAt (3.5, true) - 0.05f) < 1e-4f, "offline render of duplicate matches");

        std::cout << "\n[6] Shorter / longer file than the original clip" << std::endl;
        p.regionA->update (p.regionProps (0.0, 0.0, 2.0));
        writeStem (stem, 1.0, 0.3f);
        check (p.waitForValue (0.5, 0.3f) >= 0, "short file loaded");
        check (std::abs (p.valueAt (1.5)) < 1e-9f, "past the end of a short file is silent");
        writeStem (stem, 3.0, 0.6f);
        check (p.waitForValue (1.5, 0.6f) >= 0, "long file loaded");
        p.regionB->update (p.regionProps (3.0, 1.5, 1.5)); // asks for source 1.5..3.0; the clip ends at 2.0
        check (std::abs (p.valueAt (3.25) - 0.6f) < 1e-6f, "inside the original length plays");
        check (std::abs (p.valueAt (4.25)) < 1e-9f, "beyond the original clip length is not used");

        std::cout << "\n[7] Broken / missing / wrong-rate files" << std::endl;
        stem.replaceWithText ("not a wav file");
        p.idle (1500);
        check (std::abs (p.valueAt (0.5) - 0.6f) < 1e-6f, "broken file: previous content keeps playing");
        writeStem (stem, 2.0, 0.6f);
        p.waitForValue (0.5, 0.6f);
        stem.deleteFile();
        p.idle (1500);
        check (std::abs (p.valueAt (0.5) - 0.6f) < 1e-6f, "deleted file: previous content keeps playing");
        writeStem (stem, 2.0, 0.7f, 44100.0);
        check (p.waitForValue (0.5, hostValue) >= 0, "44.1 kHz file in a 48 kHz project: falls back to host audio (phase 1)");

        std::cout << "\n[8] Store the binding in the ARA archive" << std::endl;
        writeStem (stem, 2.0, 0.9f);
        p.waitForValue (0.5, 0.9f);
        storedArchive = p.storeArchive();
        check (storedArchive.getSize() > 0, "archive written (" + String ((int) storedArchive.getSize()) + " bytes)");
        check (archiveContains (storedArchive, hostSource.persistentID), "archive contains the audio source ID");
        check (archiveContains (storedArchive, stem.getFullPathName()), "archive contains the (non-ASCII) file path");
    }

    {
        std::cout << "\n[9] Reopen: a new document restores the binding" << std::endl;
        Project reopened (*types[0]);
        check (reopened.valid, "second project created");

        if (reopened.valid)
        {
            reopened.restoreArchive (storedArchive);
            check (reopened.waitForValue (0.5, 0.9f) >= 0, "restored project plays the shared file");
        }
    }

    dir.deleteRecursively();
    std::cout << "\n" << (failures == 0 ? "ALL HOST TESTS PASSED" : String (failures) + " HOST TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: livestem_ara_hosttest <plug-in path>" << std::endl;
        return 2;
    }

    ScopedJuceInitialiser_GUI init;
    return runTests (File (CharPointer_UTF8 (argv[1])));
}
