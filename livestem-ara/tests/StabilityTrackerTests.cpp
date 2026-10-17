#include "livestem/StabilityTracker.h"

#include <doctest/doctest.h>

using namespace livestem;

namespace
{
FileSignature sig (int64_t size, int64_t modified)
{
    return { true, size, modified };
}
} // namespace

TEST_CASE ("loads only after the file has been unchanged for the settle time")
{
    StabilityTracker t (500);
    CHECK_FALSE (t.observe (sig (100, 1), 0));
    CHECK_FALSE (t.observe (sig (100, 1), 250));
    CHECK_FALSE (t.observe (sig (100, 1), 499));
    CHECK (t.observe (sig (100, 1), 500));
}

TEST_CASE ("a growing file is never loaded while it grows")
{
    StabilityTracker t (500);
    int64_t now = 0;

    for (int64_t size = 100; size < 2000; size += 100, now += 400)
        CHECK_FALSE (t.observe (sig (size, now), now));

    CHECK_FALSE (t.observe (sig (1900, 7200), now));
    CHECK (t.observe (sig (1900, 7200), now + 500));
}

TEST_CASE ("modification time alone restarts the settle period")
{
    StabilityTracker t (500);
    CHECK_FALSE (t.observe (sig (100, 1), 0));
    CHECK_FALSE (t.observe (sig (100, 2), 400));
    CHECK_FALSE (t.observe (sig (100, 2), 800));
    CHECK (t.observe (sig (100, 2), 900));
}

TEST_CASE ("a handled signature is not loaded again until the file changes")
{
    StabilityTracker t (500);
    t.observe (sig (100, 1), 0);
    REQUIRE (t.observe (sig (100, 1), 500));
    t.markHandled (sig (100, 1));

    CHECK_FALSE (t.observe (sig (100, 1), 1000));
    CHECK_FALSE (t.observe (sig (100, 1), 5000));
    CHECK (t.wasHandled (sig (100, 1)));

    CHECK_FALSE (t.observe (sig (200, 2), 6000));
    CHECK (t.observe (sig (200, 2), 6500));
}

TEST_CASE ("a missing file is never loaded")
{
    StabilityTracker t (500);
    CHECK_FALSE (t.observe ({}, 0));
    CHECK_FALSE (t.observe ({}, 10000));
}

TEST_CASE ("reset forgets the handled signature")
{
    StabilityTracker t (500);
    t.observe (sig (100, 1), 0);
    t.markHandled (sig (100, 1));
    t.reset();
    CHECK_FALSE (t.observe (sig (100, 1), 1000));
    CHECK (t.observe (sig (100, 1), 1500));
}
