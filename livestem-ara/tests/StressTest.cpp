// Rewrites a shared WAV over and over while a simulated audio thread renders from it.
// Checks: no crash, every rendered block comes from exactly one complete file version
// (no torn reads / noise), and retired buffers are released.
//
// Usage: livestem_stress [seconds = 10] [rewriteIntervalMs = 1000]

#include "TestHelpers.h"

#include "livestem/RegionPlan.h"
#include "livestem/StemFile.h"
#include "livestem/StemWatcher.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace livestem;
using namespace std::chrono;

int main (int argc, char** argv)
{
    const int seconds = argc > 1 ? std::atoi (argv[1]) : 10;
    const int rewriteMs = argc > 2 ? std::atoi (argv[2]) : 1000;

    test::TempDir dir;
    const auto file = dir.path / "bob_bass.wav";
    const int64_t frames = 48000 * 5;

    // Version n is a constant signal of value n / 1000 on every sample.
    auto writeVersion = [&] (int n, bool atomicReplace)
    {
        const auto data = test::makeConstant (2, frames + (n % 3) * 4800, (float) n / 1000.0f);

        if (atomicReplace)
        {
            const auto tmp = dir.path / ".syncthing.bob_bass.wav.tmp";
            writeWavFile (tmp, data, WavSampleFormat::float32);

            // On Windows a rename fails while the watcher has the file open; retry like Syncthing does.
            for (std::error_code ec; ; std::this_thread::sleep_for (milliseconds (5)))
            {
                std::filesystem::rename (tmp, file, ec);

                if (! ec)
                    break;
            }
        }
        else
        {
            while (! writeWavFile (file, data, WavSampleFormat::float32))
                std::this_thread::sleep_for (milliseconds (5));
        }
    };

    writeVersion (1, true);

    auto stem = std::make_shared<StemFile> (200);
    stem->setPath (file);

    StemWatcher watcher (50);
    watcher.add (stem);
    watcher.start();

    std::atomic<bool> stop { false };
    std::atomic<long> blocks { 0 }, tornBlocks { 0 }, blocksWithAudio { 0 };
    std::atomic<int> maxRetired { 0 };

    std::thread audio ([&]
    {
        constexpr int blockSize = 256;
        std::vector<float> l (blockSize), r (blockSize);
        float* out[] { l.data(), r.data() };
        const RegionTiming region { 0, frames, 0, frames };
        int64_t pos = 0;

        while (! stop)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);

            {
                auto scope = stem->buffer().read();

                if (scope)
                {
                    const auto plan = planRegionRead (pos, blockSize, region, scope->numFrames);
                    addFromBuffer (*scope.get(), plan, out, 2);
                    blocksWithAudio++;

                    // All samples in the block must belong to one version (left) and right = left + 0.25.
                    const float v = l[(size_t) plan.outputStart];

                    for (int i = plan.outputStart; i < plan.outputStart + plan.numFromBuffer; ++i)
                        if (l[(size_t) i] != v || std::abs (r[(size_t) i] - (v + 0.25f)) > 1e-6f)
                        {
                            tornBlocks++;
                            break;
                        }
                }
            }

            pos = (pos + blockSize) % frames;
            blocks++;
            std::this_thread::sleep_for (microseconds (500));
        }
    });

    const auto end = steady_clock::now() + seconds * 1s;
    int version = 1;

    while (steady_clock::now() < end)
    {
        std::this_thread::sleep_for (milliseconds (rewriteMs));
        ++version;
        writeVersion (version, version % 2 == 0);
        maxRetired = std::max (maxRetired.load(), (int) stem->buffer().numRetired());
    }

    // Let the last version settle and load.
    std::this_thread::sleep_for (milliseconds (800));
    stop = true;
    audio.join();
    watcher.stop();

    stem->poll (StemWatcher::nowMs()); // runs garbage collection with no readers
    const auto info = stem->getInfo();
    const auto retiredAtEnd = stem->buffer().numRetired();

    float lastValue;
    {
        auto scope = stem->buffer().read();
        lastValue = scope ? scope->channels[0][0] : -1.0f;
    }

    std::printf ("versions written: %d, buffer swaps: %llu, blocks: %ld (with audio %ld), torn blocks: %ld, max retired: %d, retired at end: %zu, last value %.3f (expected %.3f)\n",
                 version, (unsigned long long) info.version, blocks.load(), blocksWithAudio.load(), tornBlocks.load(),
                 maxRetired.load(), retiredAtEnd, lastValue, (float) version / 1000.0f);

    const bool ok = tornBlocks == 0
                 && retiredAtEnd == 0
                 && std::abs (lastValue - (float) version / 1000.0f) < 1e-6f
                 && info.status == StemStatus::ready;

    std::printf ("%s\n", ok ? "STRESS TEST PASSED" : "STRESS TEST FAILED");
    return ok ? 0 : 1;
}
