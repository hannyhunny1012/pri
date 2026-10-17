#include "TestHelpers.h"

#include "livestem/RegionPlan.h"

#include <doctest/doctest.h>

#include <vector>

using namespace livestem;

namespace
{
// Region on the timeline at 1000..2000, playing source 500..1500 of a 10000-sample source.
RegionTiming typicalRegion()
{
    return { 1000, 2000, 500, 10000 };
}
} // namespace

TEST_CASE ("block entirely before or after the region produces nothing")
{
    CHECK (planRegionRead (0, 512, typicalRegion(), 10000).isEmpty());
    CHECK (planRegionRead (2000, 512, typicalRegion(), 10000).isEmpty());
    CHECK (planRegionRead (488, 512, typicalRegion(), 10000).isEmpty()); // ends exactly at 1000
}

TEST_CASE ("block straddling the region start")
{
    const auto plan = planRegionRead (900, 512, typicalRegion(), 10000);
    CHECK (plan.outputStart == 100);
    CHECK (plan.numSamples == 412);
    CHECK (plan.readStart == 500);
    CHECK (plan.numFromBuffer == 412);
}

TEST_CASE ("block straddling the region end")
{
    const auto plan = planRegionRead (1800, 512, typicalRegion(), 10000);
    CHECK (plan.outputStart == 0);
    CHECK (plan.numSamples == 200);
    CHECK (plan.readStart == 1300);
}

TEST_CASE ("buffer shorter than the source: the missing part is silent")
{
    // Buffer only has 1400 samples; region reads source 500..1500.
    const auto plan = planRegionRead (1800, 512, typicalRegion(), 1400);
    CHECK (plan.numSamples == 200);
    CHECK (plan.readStart == 1300);
    CHECK (plan.numFromBuffer == 100);

    const auto pastEnd = planRegionRead (1950, 512, typicalRegion(), 1400);
    CHECK (pastEnd.numSamples == 50);
    CHECK (pastEnd.numFromBuffer == 0);
}

TEST_CASE ("buffer longer than the source: nothing past the source length is used")
{
    // Region extends past the end of a 1000-sample source.
    const RegionTiming region { 0, 5000, 0, 1000 };
    const auto plan = planRegionRead (900, 512, region, 100000);
    CHECK (plan.outputStart == 0);
    CHECK (plan.numSamples == 100);
    CHECK (plan.numFromBuffer == 100);

    CHECK (planRegionRead (1000, 512, region, 100000).isEmpty());
}

TEST_CASE ("region starting before source sample 0 skips the gap")
{
    // Source start -200 means the first 200 region samples have no source audio.
    const RegionTiming region { 1000, 2000, -200, 10000 };
    const auto plan = planRegionRead (1000, 512, region, 10000);
    CHECK (plan.outputStart == 200);
    CHECK (plan.readStart == 0);
    CHECK (plan.numSamples == 312);
}

TEST_CASE ("duplicated regions map to the same source range independently")
{
    const RegionTiming original { 0, 1000, 0, 48000 };
    const RegionTiming copy { 48000, 49000, 0, 48000 };
    const auto a = planRegionRead (0, 256, original, 48000);
    const auto b = planRegionRead (48000, 256, copy, 48000);
    CHECK (a.readStart == b.readStart);
    CHECK (a.numSamples == b.numSamples);
}

TEST_CASE ("makeRegionTiming rounds to the nearest sample")
{
    const auto t = makeRegionTiming (1.0, 2.0, 0.5, 10.0, 48000.0);
    CHECK (t.playbackStart == 48000);
    CHECK (t.playbackEnd == 96000);
    CHECK (t.sourceStart == 24000);
    CHECK (t.sourceLength == 480000);

    CHECK (makeRegionTiming (0.00001, 1.0, 0.0, 1.0, 48000.0).playbackStart == 0);
    CHECK (makeRegionTiming (0.00002, 1.0, 0.0, 1.0, 48000.0).playbackStart == 1);
}

TEST_CASE ("addFromBuffer mixes with channel mapping")
{
    const RegionReadPlan plan { 2, 4, 10, 3 };

    SUBCASE ("stereo to stereo")
    {
        const auto data = test::makeConstant (2, 100, 0.5f);
        std::vector<float> l (8, 0.0f), r (8, 0.0f);
        float* out[] { l.data(), r.data() };
        addFromBuffer (data, plan, out, 2);
        CHECK (l == std::vector<float> { 0, 0, 0.5f, 0.5f, 0.5f, 0, 0, 0 });
        CHECK (r == std::vector<float> { 0, 0, 0.75f, 0.75f, 0.75f, 0, 0, 0 });
    }

    SUBCASE ("mono is duplicated to both channels")
    {
        const auto data = test::makeConstant (1, 100, 0.5f);
        std::vector<float> l (8, 0.0f), r (8, 0.0f);
        float* out[] { l.data(), r.data() };
        addFromBuffer (data, plan, out, 2);
        CHECK (l[3] == 0.5f);
        CHECK (r[3] == 0.5f);
    }

    SUBCASE ("stereo into mono is averaged")
    {
        const auto data = test::makeConstant (2, 100, 0.5f);
        std::vector<float> m (8, 0.0f);
        float* out[] { m.data() };
        addFromBuffer (data, plan, out, 1);
        CHECK (m[3] == doctest::Approx (0.625f));
    }

    SUBCASE ("adds on top of existing output")
    {
        const auto data = test::makeConstant (1, 100, 0.5f);
        std::vector<float> m (8, 1.0f);
        float* out[] { m.data() };
        addFromBuffer (data, plan, out, 1);
        CHECK (m[2] == 1.5f);
        CHECK (m[5] == 1.0f);
    }

    SUBCASE ("reads from readStart")
    {
        const auto data = test::makeRamp (1, 100);
        std::vector<float> m (8, 0.0f);
        float* out[] { m.data() };
        addFromBuffer (data, plan, out, 1);
        CHECK (m[2] == doctest::Approx (0.010f));
        CHECK (m[4] == doctest::Approx (0.012f));
    }
}
