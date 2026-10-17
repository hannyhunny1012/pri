#pragma once

#include <atomic>
#include <thread>

namespace livestem
{

/** Keeps rendering out of the ARA model while the host edits it, without locks on the audio thread.

    The audio thread calls tryEnterRender(); if it returns false the model is being edited and the
    block must not touch it. The editing thread calls beginEdit(), which waits (off the audio thread)
    until in-flight renders have left.
*/
class EditGuard
{
public:
    bool tryEnterRender() noexcept
    {
        renders.fetch_add (1);

        if (editing.load())
        {
            renders.fetch_sub (1);
            return false;
        }

        return true;
    }

    void exitRender() noexcept { renders.fetch_sub (1); }

    void beginEdit() noexcept
    {
        editing.store (true);

        while (renders.load() != 0)
            std::this_thread::yield();
    }

    void endEdit() noexcept { editing.store (false); }

    class RenderScope
    {
    public:
        explicit RenderScope (EditGuard& g) noexcept : guard (g), entered (g.tryEnterRender()) {}
        ~RenderScope() { if (entered) guard.exitRender(); }
        RenderScope (const RenderScope&) = delete;
        RenderScope& operator= (const RenderScope&) = delete;

        bool isEntered() const noexcept { return entered; }

    private:
        EditGuard& guard;
        const bool entered;
    };

private:
    std::atomic<int> renders { 0 };
    std::atomic<bool> editing { false };
};

} // namespace livestem
