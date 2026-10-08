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
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) noexcept {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void cc(const char (&s)[5]) noexcept {
        for (int i = 0; i < 4; ++i)
            u8(static_cast<std::uint8_t>(s[i]));
    }
};

constexpr std::uint32_t kStrl = 4 + (8 + 56) + (8 + 40);
constexpr std::uint32_t kHdrl = 4 + (8 + 56) + 8 + kStrl;
constexpr std::uint32_t kJunkAt = 12 + 8 + kHdrl;
constexpr std::uint32_t kMoviListAt = kHeaderBytes - 12;
static_assert(kJunkAt + 8 < kMoviListAt, "the header's lists must leave room for JUNK");

constexpr std::uint32_t clamp32(std::uint64_t v) noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, 0xFFFF'FFFFu));
}

}  // namespace

void header(const Fields& f, Header& out) noexcept {
    out.fill(std::byte{0});
    Put p{out};
    const std::uint32_t scale = f.scale != 0 ? f.scale : kDefaultVtime;
    const std::uint32_t rate = f.rate != 0 ? f.rate : kTickHz;
    const std::uint64_t riff = kHeaderBytes - 8 + f.movi_bytes + f.index_bytes;
    p.cc("RIFF");
    p.u32(clamp32(riff));
    p.cc("AVI ");
    p.cc("LIST");
    p.u32(kHdrl);
    p.cc("hdrl");
    p.cc("avih");
    p.u32(56);
    p.u32(static_cast<std::uint32_t>(std::uint64_t{scale} * 1'000'000u / rate));
    p.u32(0);
    p.u32(0);
    p.u32(f.index_bytes != 0 ? kHasIndex : 0u);
    p.u32(f.frames);
    p.u32(0);
    p.u32(1);
    p.u32(f.max_chunk);
    p.u32(f.width);
    p.u32(f.height);
    p.at += 16;
    p.cc("LIST");
    p.u32(kStrl);
    p.cc("strl");
    p.cc("strh");
    p.u32(56);
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
    p.u32(0xFFFF'FFFFu);
    p.u32(0);
    p.u16(0);
    p.u16(0);
    p.u16(f.width);
    p.u16(f.height);
    p.cc("strf");
    p.u32(40);
    p.u32(40);
    p.u32(f.width);
    p.u32(f.height);
    p.u16(1);
    const bool zmbv = f.codec == RecCodec::Zmbv;
    p.u16(zmbv ? 32 : 24);
    if (zmbv)
        p.cc("ZMBV");
    else
        p.cc("CSCD");
    const std::uint32_t image =
        zmbv ? static_cast<std::uint32_t>(f.width) * f.height * 4u
             : static_cast<std::uint32_t>(CscdCodec::picture_bytes(f.width, f.height));
    p.u32(image);
    p.at += 16;
    p.cc("JUNK");
    p.u32(kMoviListAt - kJunkAt - 8);
    p.at = kMoviListAt;
    p.cc("LIST");
    p.u32(clamp32(4 + f.movi_bytes));
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

void index_entry(bool key, std::uint32_t offset, std::uint32_t payload,
                 std::span<std::byte, kIndexEntry> out) noexcept {
    Put p{out};
    p.cc("00dc");
    p.u32(key ? kKeyFlag : 0u);
    p.u32(offset);
    p.u32(payload);
}

}  // namespace mister::app::avi
