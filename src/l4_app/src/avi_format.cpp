// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_format.h"

#include <algorithm>

#include "app/cscd_codec.h"

namespace mister::app::avi {

namespace {

struct Put {
    std::span<std::byte> out;
    std::size_t at = 0;

    void u8(std::uint8_t v) noexcept { out[at++] = std::byte{v}; }

    void u16(std::uint16_t v) noexcept {
        constexpr unsigned kByteBits = 8;
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> kByteBits));
    }

    void u32(std::uint32_t v) noexcept {
        constexpr unsigned kWordBits = 16;
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> kWordBits));
    }

    void cc(const char (&s)[5]) noexcept {
        constexpr int kFourccBytes = 4;
        for (int i = 0; i < kFourccBytes; ++i)
            u8(static_cast<std::uint8_t>(s[i]));
    }
};

constexpr std::uint32_t kStrl = 4 + (8 + 56) + (8 + 40);
constexpr std::uint32_t kHdrl = 4 + (8 + 56) + 8 + kStrl;
constexpr std::uint32_t kJunkAt = 12 + 8 + kHdrl;
constexpr std::uint32_t kMoviListAt = kHeaderBytes - 12;
static_assert(kJunkAt + 8 < kMoviListAt, "the header's lists must leave room for JUNK");

constexpr std::uint32_t clamp32(std::uint64_t v) noexcept {
    constexpr std::uint32_t kMaxU32 = 0xFFFF'FFFFu;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, kMaxU32));
}

}

void header(const Fields& f, Header& out) noexcept {
    constexpr std::uint32_t kChunkHeadBytes = 8;
    constexpr std::uint32_t kAvihBytes = 56;
    constexpr std::uint32_t kStrhBytes = 56;
    constexpr std::uint32_t kStrfBytes = 40;
    constexpr std::size_t kAvihReservedBytes = 16;
    constexpr std::size_t kStrfTrailBytes = 16;
    constexpr std::uint32_t kMicrosPerSecond = 1'000'000;
    constexpr std::uint16_t kBitsPerPixelZmbv = 32;
    constexpr std::uint16_t kBitsPerPixelCscd = 24;
    constexpr std::uint32_t kBytesPerPixelZmbv = 4;
    constexpr std::uint32_t kListTypeBytes = 4;
    constexpr std::uint32_t kNoQuality = 0xFFFF'FFFFu;
    constexpr std::uint32_t kStreamCount = 1;
    out.fill(std::byte{0});
    Put p{out};
    const std::uint32_t scale = f.scale != 0 ? f.scale : kDefaultVtime;
    const std::uint32_t rate = f.rate != 0 ? f.rate : kTickHz;
    const std::uint64_t riff = kHeaderBytes - kChunkHeadBytes + f.movi_bytes + f.index_bytes;
    p.cc("RIFF");
    p.u32(clamp32(riff));
    p.cc("AVI ");
    p.cc("LIST");
    p.u32(kHdrl);
    p.cc("hdrl");
    p.cc("avih");
    p.u32(kAvihBytes);
    p.u32(static_cast<std::uint32_t>(std::uint64_t{scale} * kMicrosPerSecond / rate));
    p.u32(0);
    p.u32(0);
    p.u32(f.index_bytes != 0 ? kHasIndex : 0u);
    p.u32(f.frames);
    p.u32(0);
    p.u32(kStreamCount);
    p.u32(f.max_chunk);
    p.u32(f.width);
    p.u32(f.height);
    p.at += kAvihReservedBytes;
    p.cc("LIST");
    p.u32(kStrl);
    p.cc("strl");
    p.cc("strh");
    p.u32(kStrhBytes);
    p.cc("vids");
    if (f.codec == RecCodec::Zmbv)
        p.cc("ZMBV");
    else
        p.cc("CSCD");
    p.u32(0);
    p.u16(0);
    p.u16(0);
    p.u32(0);
    p.u32(scale);
    p.u32(rate);
    p.u32(0);
    p.u32(f.frames);
    p.u32(f.max_chunk);
    p.u32(kNoQuality);
    p.u32(0);
    p.u16(0);
    p.u16(0);
    p.u16(f.width);
    p.u16(f.height);
    p.cc("strf");
    p.u32(kStrfBytes);
    p.u32(kStrfBytes);
    p.u32(f.width);
    p.u32(f.height);
    p.u16(1);
    const bool zmbv = f.codec == RecCodec::Zmbv;
    p.u16(zmbv ? kBitsPerPixelZmbv : kBitsPerPixelCscd);
    if (zmbv)
        p.cc("ZMBV");
    else
        p.cc("CSCD");
    const std::uint32_t image = zmbv
        ? static_cast<std::uint32_t>(f.width) * f.height * kBytesPerPixelZmbv
        : static_cast<std::uint32_t>(CscdCodec::picture_bytes(f.width, f.height));
    p.u32(image);
    p.at += kStrfTrailBytes;
    p.cc("JUNK");
    p.u32(kMoviListAt - kJunkAt - kChunkHeadBytes);
    p.at = kMoviListAt;
    p.cc("LIST");
    p.u32(clamp32(kListTypeBytes + f.movi_bytes));
    p.cc("movi");
}

void chunk_head(std::uint32_t payload, std::span<std::byte, kChunkHead> out) noexcept {
    Put p{out};
    p.cc("00dc");
    p.u32(payload);
}

void index_head(std::uint32_t entries, std::span<std::byte, kChunkHead> out) noexcept {
    Put p{out};
    p.cc("idx1");
    p.u32(entries * static_cast<std::uint32_t>(kIndexEntry));
}

void index_entry(
    bool key,
    std::uint32_t offset,
    std::uint32_t payload,
    std::span<std::byte, kIndexEntry> out
) noexcept {
    Put p{out};
    p.cc("00dc");
    p.u32(key ? kKeyFlag : 0u);
    p.u32(offset);
    p.u32(payload);
}

}
