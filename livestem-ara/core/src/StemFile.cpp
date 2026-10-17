#include "livestem/StemFile.h"

#include "livestem/WavFile.h"

#include <chrono>

namespace livestem
{

const char* statusName (StemStatus status) noexcept
{
    switch (status)
    {
        case StemStatus::unassigned: return "未対応付け";
        case StemStatus::waiting:    return "更新待ち";
        case StemStatus::loading:    return "読み込み中";
        case StemStatus::ready:      return "最新";
        case StemStatus::error:      return "エラー";
        case StemStatus::missing:    return "ファイルなし";
    }

    return "";
}

FileSignature StemFile::signatureOf (const std::filesystem::path& file)
{
    FileSignature sig;
    std::error_code ec;
    const auto size = std::filesystem::file_size (file, ec);

    if (ec)
        return sig;

    const auto modified = std::filesystem::last_write_time (file, ec);

    if (ec)
        return sig;

    sig.exists = true;
    sig.size = (int64_t) size;
    sig.modifiedNs = (int64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (modified.time_since_epoch()).count();
    return sig;
}

void StemFile::setPath (const std::filesystem::path& newPath)
{
    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (newPath == path)
            return;

        path = newPath;
        ++pathGeneration;
        info = {};
        info.path = newPath;
        info.status = newPath.empty() ? StemStatus::unassigned : StemStatus::waiting;
        info.version = version.load() + 1;
    }

    // Content from a different file must not keep playing.
    slot.publish (nullptr);
    version.fetch_add (1);
}

std::filesystem::path StemFile::getPath() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return path;
}

StemInfo StemFile::getInfo() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    auto copy = info;
    copy.version = version.load();
    return copy;
}

void StemFile::setStatus (StemStatus status, std::string message)
{
    const std::lock_guard<std::mutex> lock (mutex);
    info.status = status;
    info.message = std::move (message);
}

void StemFile::poll (int64_t nowMs)
{
    slot.collectGarbage();

    std::filesystem::path file;
    uint64_t generation;

    {
        const std::lock_guard<std::mutex> lock (mutex);
        file = path;
        generation = pathGeneration;
    }

    if (generation != trackedGeneration)
    {
        tracker.reset();
        trackedGeneration = generation;
    }

    if (file.empty())
        return;

    const auto sig = signatureOf (file);

    if (! sig.exists)
    {
        const bool hasContent = slot.getShared() != nullptr;
        setStatus (StemStatus::missing, hasContent ? "ファイルが見つかりません（直前の内容を再生中）" : "ファイルが見つかりません（元の音声を再生中）");
        tracker.observe (sig, nowMs);
        return;
    }

    if (! tracker.observe (sig, nowMs))
    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (! tracker.wasHandled (sig))
            info.status = StemStatus::waiting;
        else if (info.status == StemStatus::missing)
            info.status = lastLoadSucceeded ? StemStatus::ready : StemStatus::error;

        return;
    }

    setStatus (StemStatus::loading, {});
    auto result = readWavFile (file);

    // If the file changed while reading, drop the result and wait for it to settle again.
    if (signatureOf (file) != sig)
    {
        setStatus (StemStatus::waiting, {});
        return;
    }

    tracker.markHandled (sig);

    {
        const std::lock_guard<std::mutex> lock (mutex);

        if (generation != pathGeneration)
            return; // path changed while loading
    }

    lastLoadSucceeded = result.ok();

    if (! result.ok())
    {
        setStatus (StemStatus::error, result.error);
        return;
    }

    const auto loaded = result.data;
    slot.publish (std::move (result.data));

    {
        const std::lock_guard<std::mutex> lock (mutex);
        info.status = StemStatus::ready;
        info.message.clear();
        info.loadedAtUnixMs = (int64_t) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
        info.sampleRate = loaded->sampleRate;
        info.numChannels = loaded->numChannels();
        info.numFrames = loaded->numFrames;
    }

    version.fetch_add (1);
}

} // namespace livestem
