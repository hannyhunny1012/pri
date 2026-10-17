#include "TestHelpers.h"

#include "livestem/StemFile.h"
#include "livestem/StemWatcher.h"

#include <doctest/doctest.h>

#include <fstream>

using namespace livestem;

namespace
{
// Drives poll() with a fake clock so tests don't sleep.
struct Harness
{
    explicit Harness (const std::filesystem::path& file) { stem.setPath (file); }

    void pollFor (int64_t ms)
    {
        for (int64_t end = now + ms; now <= end; now += 100)
            stem.poll (now);
    }

    StemFile stem { 500 };
    int64_t now = 0;
};

float firstSample (const StemFile& stem)
{
    auto scope = stem.buffer().read();
    return scope ? scope->channels[0][0] : -100.0f;
}
} // namespace

TEST_CASE ("loads a file once it has settled")
{
    test::TempDir dir;
    const auto file = dir.path / "alice_gtr.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (2, 4800, 0.1f), WavSampleFormat::float32));

    Harness h (file);
    h.stem.poll (h.now);
    CHECK (h.stem.getInfo().status == StemStatus::waiting);
    CHECK (h.stem.buffer().read().get() == nullptr);

    h.pollFor (600);
    const auto info = h.stem.getInfo();
    CHECK (info.status == StemStatus::ready);
    CHECK (info.numFrames == 4800);
    CHECK (info.numChannels == 2);
    CHECK (firstSample (h.stem) == doctest::Approx (0.1f));
}

TEST_CASE ("replacing the file swaps the buffer and bumps the version")
{
    test::TempDir dir;
    const auto file = dir.path / "s.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (1, 4800, 0.1f), WavSampleFormat::float32));

    Harness h (file);
    h.pollFor (600);
    const auto v1 = h.stem.getVersion();

    // Different length guarantees a different signature even on coarse mtime filesystems.
    REQUIRE (writeWavFile (file, test::makeConstant (1, 9600, 0.7f), WavSampleFormat::float32));
    h.pollFor (600);

    CHECK (h.stem.getVersion() > v1);
    CHECK (firstSample (h.stem) == doctest::Approx (0.7f));
    CHECK (h.stem.getInfo().numFrames == 9600);
}

TEST_CASE ("shorter and longer replacements are loaded as-is")
{
    test::TempDir dir;
    const auto file = dir.path / "s.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (2, 48000, 0.1f), WavSampleFormat::float32));

    Harness h (file);
    h.pollFor (600);

    REQUIRE (writeWavFile (file, test::makeConstant (2, 1000, 0.2f), WavSampleFormat::float32));
    h.pollFor (600);
    CHECK (h.stem.getInfo().numFrames == 1000);

    REQUIRE (writeWavFile (file, test::makeConstant (2, 96000, 0.3f), WavSampleFormat::float32));
    h.pollFor (600);
    CHECK (h.stem.getInfo().numFrames == 96000);
}

TEST_CASE ("a broken file keeps the previous content and reports an error")
{
    test::TempDir dir;
    const auto file = dir.path / "s.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (1, 4800, 0.4f), WavSampleFormat::float32));

    Harness h (file);
    h.pollFor (600);
    REQUIRE (h.stem.getInfo().status == StemStatus::ready);

    std::ofstream (file, std::ios::trunc) << "garbage that is not a wav file";
    h.pollFor (600);

    const auto info = h.stem.getInfo();
    CHECK (info.status == StemStatus::error);
    CHECK_FALSE (info.message.empty());
    CHECK (firstSample (h.stem) == doctest::Approx (0.4f));

    // Stays in error without retrying until the file changes, then recovers.
    h.pollFor (2000);
    CHECK (h.stem.getInfo().status == StemStatus::error);

    REQUIRE (writeWavFile (file, test::makeConstant (1, 4800, 0.9f), WavSampleFormat::float32));
    h.pollFor (600);
    CHECK (h.stem.getInfo().status == StemStatus::ready);
    CHECK (firstSample (h.stem) == doctest::Approx (0.9f));
}

TEST_CASE ("missing file: no buffer, status missing")
{
    test::TempDir dir;
    Harness h (dir.path / "absent.wav");
    h.pollFor (1000);
    CHECK (h.stem.getInfo().status == StemStatus::missing);
    CHECK (h.stem.buffer().read().get() == nullptr);
}

TEST_CASE ("file that disappears after loading keeps playing the last content")
{
    test::TempDir dir;
    const auto file = dir.path / "s.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (1, 4800, 0.4f), WavSampleFormat::float32));
    Harness h (file);
    h.pollFor (600);

    std::filesystem::remove (file);
    h.pollFor (600);
    CHECK (h.stem.getInfo().status == StemStatus::missing);
    CHECK (firstSample (h.stem) == doctest::Approx (0.4f));
}

TEST_CASE ("changing the path drops the old content")
{
    test::TempDir dir;
    const auto a = dir.path / "a.wav";
    const auto b = dir.path / "b.wav";
    REQUIRE (writeWavFile (a, test::makeConstant (1, 4800, 0.1f), WavSampleFormat::float32));
    REQUIRE (writeWavFile (b, test::makeConstant (1, 4800, 0.2f), WavSampleFormat::float32));

    Harness h (a);
    h.pollFor (600);
    REQUIRE (firstSample (h.stem) == doctest::Approx (0.1f));

    h.stem.setPath (b);
    CHECK (h.stem.buffer().read().get() == nullptr);
    h.pollFor (600);
    CHECK (firstSample (h.stem) == doctest::Approx (0.2f));

    h.stem.setPath ({});
    h.pollFor (600);
    CHECK (h.stem.getInfo().status == StemStatus::unassigned);
    CHECK (h.stem.buffer().read().get() == nullptr);
}

TEST_CASE ("watcher thread picks up a file in real time")
{
    test::TempDir dir;
    const auto file = dir.path / "s.wav";
    REQUIRE (writeWavFile (file, test::makeConstant (1, 4800, 0.5f), WavSampleFormat::float32));

    auto stem = std::make_shared<StemFile> (200);
    stem->setPath (file);

    StemWatcher watcher (50);
    watcher.add (stem);
    watcher.start();

    for (int i = 0; i < 100 && stem->getInfo().status != StemStatus::ready; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (20));

    watcher.stop();
    CHECK (stem->getInfo().status == StemStatus::ready);
}
