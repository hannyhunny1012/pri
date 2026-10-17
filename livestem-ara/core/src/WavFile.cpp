#include "livestem/WavFile.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace livestem
{

namespace
{
constexpr uint16_t formatPcm = 1;
constexpr uint16_t formatFloat = 3;
constexpr uint16_t formatExtensible = 0xfffe;

uint16_t readU16 (const uint8_t* p) noexcept { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t readU32 (const uint8_t* p) noexcept { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint64_t readU64 (const uint8_t* p) noexcept { return (uint64_t) readU32 (p) | ((uint64_t) readU32 (p + 4) << 32); }

void writeU16 (std::ostream& out, uint16_t v) { const uint8_t b[] { (uint8_t) v, (uint8_t) (v >> 8) }; out.write ((const char*) b, 2); }
void writeU32 (std::ostream& out, uint32_t v) { const uint8_t b[] { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) }; out.write ((const char*) b, 4); }

struct Format
{
    uint16_t tag = 0;
    int numChannels = 0;
    uint32_t sampleRate = 0;
    int bitsPerSample = 0;
    int blockAlign = 0;
};

float decodeSample (const uint8_t* p, const Format& f) noexcept
{
    if (f.tag == formatFloat)
    {
        if (f.bitsPerSample == 32)
        {
            float v;
            std::memcpy (&v, p, 4);
            return v;
        }

        double v;
        std::memcpy (&v, p, 8);
        return (float) v;
    }

    switch (f.bitsPerSample)
    {
        case 8:  return ((float) p[0] - 128.0f) / 128.0f;
        case 16: return (float) (int16_t) readU16 (p) / 32768.0f;
        case 24: return (float) ((int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8) / 8388608.0f;
        case 32: return (float) ((double) (int32_t) readU32 (p) / 2147483648.0);
        default: return 0.0f;
    }
}

WavReadResult fail (std::string message)
{
    WavReadResult r;
    r.error = std::move (message);
    return r;
}
} // namespace

WavReadResult readWavFile (const std::filesystem::path& file)
{
    std::ifstream in (file, std::ios::binary);

    if (! in)
        return fail ("ファイルを開けません");

    in.seekg (0, std::ios::end);
    const auto fileSize = (uint64_t) in.tellg();
    in.seekg (0, std::ios::beg);

    uint8_t header[12];

    if (! in.read ((char*) header, 12))
        return fail ("WAVヘッダーが読めません");

    const bool isRf64 = std::memcmp (header, "RF64", 4) == 0;

    if ((std::memcmp (header, "RIFF", 4) != 0 && ! isRf64) || std::memcmp (header + 8, "WAVE", 4) != 0)
        return fail ("WAVファイルではありません");

    Format fmt;
    bool haveFormat = false;
    uint64_t ds64DataSize = 0;

    for (;;)
    {
        uint8_t chunkHeader[8];

        if (! in.read ((char*) chunkHeader, 8))
            return fail ("dataチャンクが見つかりません");

        const uint64_t chunkStart = (uint64_t) in.tellg();
        uint64_t chunkSize = readU32 (chunkHeader + 4);

        if (std::memcmp (chunkHeader, "ds64", 4) == 0 && chunkSize >= 16)
        {
            uint8_t ds64[16];
            in.read ((char*) ds64, 16);
            ds64DataSize = readU64 (ds64 + 8);
        }
        else if (std::memcmp (chunkHeader, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
                return fail ("fmtチャンクが不正です");

            std::array<uint8_t, 40> f {};
            in.read ((char*) f.data(), (std::streamsize) std::min<uint64_t> (chunkSize, f.size()));

            fmt.tag = readU16 (f.data());
            fmt.numChannels = readU16 (f.data() + 2);
            fmt.sampleRate = readU32 (f.data() + 4);
            fmt.blockAlign = readU16 (f.data() + 12);
            fmt.bitsPerSample = readU16 (f.data() + 14);

            if (fmt.tag == formatExtensible && chunkSize >= 26)
                fmt.tag = readU16 (f.data() + 24); // first two bytes of the SubFormat GUID

            const bool supported = (fmt.tag == formatPcm && (fmt.bitsPerSample == 8 || fmt.bitsPerSample == 16 || fmt.bitsPerSample == 24 || fmt.bitsPerSample == 32))
                                || (fmt.tag == formatFloat && (fmt.bitsPerSample == 32 || fmt.bitsPerSample == 64));

            if (! supported)
                return fail ("未対応のWAV形式です (format " + std::to_string (fmt.tag) + ", " + std::to_string (fmt.bitsPerSample) + "bit)");

            if (fmt.numChannels <= 0 || fmt.sampleRate == 0 || fmt.blockAlign != fmt.numChannels * fmt.bitsPerSample / 8)
                return fail ("fmtチャンクが不正です");

            haveFormat = true;
        }
        else if (std::memcmp (chunkHeader, "data", 4) == 0)
        {
            if (! haveFormat)
                return fail ("fmtチャンクがdataチャンクより後にあります");

            if (isRf64 && chunkSize == 0xffffffffu)
                chunkSize = ds64DataSize;

            // A writer that has not finished yet may leave the declared size larger than the file.
            if (chunkStart + chunkSize > fileSize)
                return fail ("ファイルが途中までしか書かれていません");

            auto data = std::make_shared<AudioData>();
            data->sampleRate = (double) fmt.sampleRate;
            data->numFrames = (int64_t) (chunkSize / (uint64_t) fmt.blockAlign);
            data->channels.assign ((size_t) fmt.numChannels, std::vector<float> ((size_t) data->numFrames));

            constexpr int64_t framesPerBlock = 65536;
            std::vector<uint8_t> raw ((size_t) (framesPerBlock * fmt.blockAlign));
            const int bytesPerSample = fmt.bitsPerSample / 8;

            for (int64_t done = 0; done < data->numFrames;)
            {
                const auto n = std::min (framesPerBlock, data->numFrames - done);

                if (! in.read ((char*) raw.data(), (std::streamsize) (n * fmt.blockAlign)))
                    return fail ("読み込み中にエラーが発生しました");

                for (int ch = 0; ch < fmt.numChannels; ++ch)
                {
                    auto* dest = data->channels[(size_t) ch].data() + done;
                    const auto* src = raw.data() + ch * bytesPerSample;

                    for (int64_t i = 0; i < n; ++i)
                        dest[i] = decodeSample (src + i * fmt.blockAlign, fmt);
                }

                done += n;
            }

            return { std::move (data), {} };
        }

        in.clear();
        in.seekg ((std::streamoff) (chunkStart + chunkSize + (chunkSize & 1)), std::ios::beg);
    }
}

bool writeWavFile (const std::filesystem::path& file, const AudioData& data, WavSampleFormat format)
{
    std::ofstream out (file, std::ios::binary | std::ios::trunc);

    if (! out)
        return false;

    const int bits = format == WavSampleFormat::int16 ? 16 : (format == WavSampleFormat::int24 ? 24 : 32);
    const auto numChannels = (uint16_t) data.numChannels();
    const auto blockAlign = (uint16_t) (numChannels * bits / 8);
    const auto dataSize = (uint32_t) (data.numFrames * blockAlign);

    out.write ("RIFF", 4);
    writeU32 (out, 36 + dataSize);
    out.write ("WAVEfmt ", 8);
    writeU32 (out, 16);
    writeU16 (out, format == WavSampleFormat::float32 ? formatFloat : formatPcm);
    writeU16 (out, numChannels);
    writeU32 (out, (uint32_t) data.sampleRate);
    writeU32 (out, (uint32_t) data.sampleRate * blockAlign);
    writeU16 (out, blockAlign);
    writeU16 (out, (uint16_t) bits);
    out.write ("data", 4);
    writeU32 (out, dataSize);

    std::vector<uint8_t> frame (blockAlign);

    for (int64_t i = 0; i < data.numFrames; ++i)
    {
        auto* p = frame.data();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float v = data.channels[(size_t) ch][(size_t) i];

            if (format == WavSampleFormat::float32)
            {
                std::memcpy (p, &v, 4);
                p += 4;
            }
            else
            {
                const double scale = format == WavSampleFormat::int16 ? 32767.0 : 8388607.0;
                const auto s = (int32_t) std::clamp ((double) v * scale, -scale - 1.0, scale);

                for (int b = 0; b < bits / 8; ++b)
                    *p++ = (uint8_t) (s >> (8 * b));
            }
        }

        out.write ((const char*) frame.data(), blockAlign);
    }

    return (bool) out;
}

} // namespace livestem
