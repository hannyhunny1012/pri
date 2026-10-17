#include "livestem/EditGuard.h"

#include <doctest/doctest.h>

#include <atomic>
#include <thread>

using namespace livestem;

TEST_CASE ("render is refused while editing")
{
    EditGuard g;
    CHECK (EditGuard::RenderScope (g).isEntered());

    g.beginEdit();
    CHECK_FALSE (EditGuard::RenderScope (g).isEntered());
    g.endEdit();

    CHECK (EditGuard::RenderScope (g).isEntered());
}

TEST_CASE ("beginEdit waits for in-flight renders and no render overlaps an edit")
{
    EditGuard g;
    std::atomic<bool> stop { false };
    std::atomic<bool> inEdit { false };
    std::atomic<int> overlaps { 0 };

    std::thread audio ([&]
    {
        while (! stop)
        {
            EditGuard::RenderScope scope (g);

            if (scope.isEntered())
            {
                for (int i = 0; i < 100; ++i)
                    if (inEdit)
                        ++overlaps;
            }
        }
    });

    for (int i = 0; i < 2000; ++i)
    {
        g.beginEdit();
        inEdit = true;
        std::this_thread::yield();
        inEdit = false;
        g.endEdit();
    }

    stop = true;
    audio.join();
    CHECK (overlaps == 0);
}
