#include "livestem/SharedSlot.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace livestem;

namespace
{
struct Tracked
{
    explicit Tracked (int v, std::atomic<int>& liveCount) : value (v), live (liveCount) { ++live; }
    ~Tracked() { value = -1; --live; }

    int value;
    std::atomic<int>& live;
};
} // namespace

TEST_CASE ("empty slot reads null")
{
    SharedSlot<int> slot;
    CHECK (slot.read().get() == nullptr);
}

TEST_CASE ("publish swaps the object seen by new readers")
{
    SharedSlot<int> slot;
    slot.publish (std::make_shared<int> (1));
    CHECK (*slot.read().get() == 1);
    slot.publish (std::make_shared<int> (2));
    CHECK (*slot.read().get() == 2);
}

TEST_CASE ("an object in use by a reader is not destroyed until the reader is done")
{
    std::atomic<int> live { 0 };
    SharedSlot<Tracked> slot;
    slot.publish (std::make_shared<Tracked> (1, live));

    {
        auto scope = slot.read();
        slot.publish (std::make_shared<Tracked> (2, live));

        CHECK (scope->value == 1);
        CHECK (live == 2);
        CHECK (slot.numRetired() == 1);

        slot.collectGarbage();
        CHECK (live == 2); // reader still active
    }

    slot.collectGarbage();
    CHECK (live == 1);
    CHECK (slot.numRetired() == 0);
}

TEST_CASE ("retired objects are destroyed by collectGarbage, not by the reader")
{
    std::atomic<int> live { 0 };
    SharedSlot<Tracked> slot;
    slot.publish (std::make_shared<Tracked> (1, live));

    auto scope = std::make_unique<SharedSlot<Tracked>::ReadScope> (slot.read());
    slot.publish (std::make_shared<Tracked> (2, live));
    scope.reset(); // reader ends: must not free anything
    CHECK (live == 2);

    slot.collectGarbage();
    CHECK (live == 1);
}

TEST_CASE ("concurrent readers never see a destroyed object")
{
    std::atomic<int> live { 0 };
    SharedSlot<Tracked> slot;
    slot.publish (std::make_shared<Tracked> (0, live));

    std::atomic<bool> stop { false };
    std::atomic<int> bad { 0 };
    std::vector<std::thread> readers;

    for (int r = 0; r < 3; ++r)
    {
        readers.emplace_back ([&]
        {
            while (! stop)
            {
                auto scope = slot.read();

                for (int i = 0; i < 50; ++i)
                    if (scope.get() == nullptr || scope->value < 0)
                        ++bad;
            }
        });
    }

    for (int i = 1; i < 20000; ++i)
    {
        slot.publish (std::make_shared<Tracked> (i, live));

        if (i % 7 == 0)
            slot.collectGarbage();
    }

    stop = true;

    for (auto& t : readers)
        t.join();

    slot.collectGarbage();
    CHECK (bad == 0);
    CHECK (live == 1);
}
