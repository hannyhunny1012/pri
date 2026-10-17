#include "TestHelpers.h"

#include <doctest/doctest.h>

#include <cmath>
#include <fstream>

using namespace livestem;

TEST_CASE ("float32 round trip is exact")
{
    test::TempDir dir;
    const auto file = dir.path / "a.wav";
    const auto original = test::makeRamp (2, 48000);
    REQUIRE (writeWavFile (file, original, WavSampleFormat::float32));

    const auto r = readWavFile (file);
    REQUIRE (r.ok());
    CHECK (r.data->sampleRate == 48000.0);
    CHECK (r.data->numChannels() == 2);
    CHECK (r.data->numFrames == 48000);
    CHECK (r.data->channels == original.channels);
}

TEST_CASE ("integer formats decode within quantisation error")
{
    test::TempDir dir;
    auto original = test::makeRamp (1, 2000, 44100.0);

    for (auto& s : original.channels[0])
        s = s * 1.6f - 0.8f;

    for (auto [format, tolerance] : { std::pair { WavSampleFormat::int16, 1.0 / 16000.0 }, std::pair { WavSampleFormat::int24, 1.0 / 4000000.0 } })
    {
        const auto file = dir.path / "b.wav";
        REQUIRE (writeWavFile (file, original, format));
        const auto r = readWavFile (file);
        REQUIRE (r.ok());
        CHECK (r.data->sampleRate == 44100.0);

        for (size_t i = 0; i < original.channels[0].size(); ++i)
            CHECK (std::abs (r.data->channels[0][i] - original.channels[0][i]) <= tolerance * 2);
    }
}

TEST_CASE ("truncated file is reported as incomplete")
{
    test::TempDir dir;
    const auto file = dir.path / "c.wav";
    REQUIRE (writeWavFile (file, test::makeRamp (2, 10000), WavSampleFormat::float32));
    std::filesystem::resize_file (file, std::filesystem::file_size (file) / 2);

    const auto r = readWavFile (file);
    CHECK_FALSE (r.ok());
    CHECK_FALSE (r.error.empty());
}

TEST_CASE ("non-WAV and missing files fail cleanly")
{
    test::TempDir dir;
    const auto file = dir.path / "d.wav";
    std::ofstream (file) << "this is not audio";
    CHECK_FALSE (readWavFile (file).ok());
    CHECK_FALSE (readWavFile (dir.path / "nope.wav").ok());
}

TEST_CASE ("unknown chunks before data are skipped")
{
    test::TempDir dir;
    const auto plain = dir.path / "e.wav";
    REQUIRE (writeWavFile (plain, test::makeRamp (1, 100), WavSampleFormat::float32));

    // Insert a LIST chunk with odd size (padded) between fmt and data.
    std::ifstream in (plain, std::ios::binary);
    std::string bytes ((std::istreambuf_iterator<char> (in)), {});
    const std::string list = std::string ("LIST") + std::string ("\x03\x00\x00\x00", 4) + "abc" + std::string (1, '\0');
    bytes.insert (36, list);
    const auto withList = dir.path / "f.wav";
    std::ofstream (withList, std::ios::binary) << bytes;

    const auto r = readWavFile (withList);
    REQUIRE (r.ok());
    CHECK (r.data->numFrames == 100);
    CHECK (r.data->channels[0][5] == doctest::Approx (0.005f));
}
