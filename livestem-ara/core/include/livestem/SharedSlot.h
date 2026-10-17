#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace livestem
{

/** Holds the latest published object for real-time readers.

    - publish() and collectGarbage() run on non-audio threads. They may lock and allocate.
    - read() is wait-free: two atomic increments/decrements and one atomic load. No locks,
      no allocation, and the object is never destroyed on the reading thread.

    Objects are kept alive by std::shared_ptr. A replaced object is moved to a retired list and
    destroyed by collectGarbage() once the reader count has been observed at zero after the swap:
    any reader that starts after that moment can only see the new pointer.
*/
template <typename T>
class SharedSlot
{
public:
    class ReadScope
    {
    public:
        explicit ReadScope (const SharedSlot& s) noexcept : slot (&s)
        {
            slot->readers.fetch_add (1);
            object = slot->current.load();
        }

        ~ReadScope() { release(); }

        ReadScope (ReadScope&& other) noexcept : slot (other.slot), object (other.object) { other.slot = nullptr; }
        ReadScope (const ReadScope&) = delete;
        ReadScope& operator= (const ReadScope&) = delete;
        ReadScope& operator= (ReadScope&&) = delete;

        const T* get() const noexcept { return object; }
        const T* operator->() const noexcept { return object; }
        explicit operator bool() const noexcept { return object != nullptr; }

    private:
        void release() noexcept
        {
            if (slot != nullptr)
                slot->readers.fetch_sub (1);

            slot = nullptr;
        }

        const SharedSlot* slot;
        const T* object = nullptr;
    };

    SharedSlot() = default;
    SharedSlot (const SharedSlot&) = delete;
    SharedSlot& operator= (const SharedSlot&) = delete;

    /** Real-time safe. Keep the returned scope alive while using the pointer. */
    ReadScope read() const noexcept { return ReadScope (*this); }

    /** Replaces the current object (nullptr clears it). Not real-time safe. */
    void publish (std::shared_ptr<const T> next)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        current.store (next.get());

        if (owner != nullptr)
            retired.push_back (std::move (owner));

        owner = std::move (next);
        collectLocked();
    }

    /** Destroys retired objects that no reader can still be using. Not real-time safe. */
    void collectGarbage()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        collectLocked();
    }

    /** Returns a strong reference to the current object (for UI and analysis threads). */
    std::shared_ptr<const T> getShared() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return owner;
    }

    size_t numRetired() const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return retired.size();
    }

private:
    void collectLocked()
    {
        if (! retired.empty() && readers.load() == 0)
            retired.clear();
    }

    std::atomic<const T*> current { nullptr };
    mutable std::atomic<int> readers { 0 };

    mutable std::mutex mutex;
    std::shared_ptr<const T> owner;
    std::vector<std::shared_ptr<const T>> retired;
};

} // namespace livestem
