// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/avi_format.h"

#include <limits>

#include "app/frame_ticks.h"
#include "app/log_clock.h"
#include "app/picture_size.h"
#include "app/row_bytes.h"
#include "infra/endian.h"

namespace mister::app::avi {

namespace {

struct Put {
    std::span<std::byte> out;
    std::size_t at = 0;

    void u8(std::uint8_t v) noexcept { out[at++] = std::byte{v}; }

    void u16(std::uint16_t v) noexcept {
        infra::store_le(out.subspan(at).first<sizeof v>(), v);
        at += sizeof v;
    }

    void u32(std::uint32_t v) noexcept {
        infra::store_le(out.subspan(at).first<sizeof v>(), v);
        at += sizeof v;
    }

    void u16(PictureRows rows) noexcept {
        rows.store_le16(out.subspan(at).first<sizeof(std::uint16_t)>());
        at += sizeof(std::uint16_t);
    }

    void u32(PictureRows rows) noexcept {
        rows.store_le32(out.subspan(at).first<sizeof(std::uint32_t)>());
        at += sizeof(std::uint32_t);
    }

    void u16(PicturePixels width) noexcept {
        width.store_le16(out.subspan(at).first<sizeof(std::uint16_t)>());
        at += sizeof(std::uint16_t);
    }

    void u32(PicturePixels width) noexcept {
        width.store_le32(out.subspan(at).first<sizeof(std::uint32_t)>());
        at += sizeof(std::uint32_t);
    }

    void u16(BitsPerPixel bits) noexcept {
        bits.store_le16(out.subspan(at).first<sizeof(std::uint16_t)>());
        at += sizeof(std::uint16_t);
    }

    void cc(const char (&s)[5]) noexcept {
        constexpr int kFourccBytes = 4;
        for (int i = 0; i < kFourccBytes; ++i)
            u8(static_cast<std::uint8_t>(s[i]));
    }
};

constexpr std::uint32_t kChunkHeadBytes = 8;
constexpr std::uint32_t kListTypeBytes = 4;
constexpr std::uint32_t kRiffHeadBytes = 12;
constexpr std::uint32_t kAvihBytes = 56;
constexpr std::uint32_t kStrhBytes = 56;
constexpr std::uint32_t kStrfBytes = 40;

constexpr std::uint32_t kStrl = kListTypeBytes
    + (kChunkHeadBytes + kStrhBytes)
    + (kChunkHeadBytes + kStrfBytes);
constexpr std::uint32_t kHdrl = kListTypeBytes
    + (kChunkHeadBytes + kAvihBytes)
    + kChunkHeadBytes
    + kStrl;
constexpr std::uint32_t kJunkAt = kRiffHeadBytes + kChunkHeadBytes + kHdrl;
constexpr std::uint32_t kMoviListAt = static_cast<std::uint32_t>(kMoviFourcc) - kChunkHeadBytes;
static_assert(kJunkAt + 8 < kMoviListAt, "the header's lists must leave room for JUNK");

constexpr std::uint32_t clamp32(std::uint64_t v) noexcept {
    return sat_u32(v);
}

}

Header header(const Fields& f) noexcept {
    constexpr std::size_t kAvihReservedBytes = 16;
    constexpr std::size_t kStrfTrailBytes = 16;
    constexpr std::uint32_t kMicrosPerSecond = 1'000'000;
    constexpr std::uint32_t kNoQuality = std::numeric_limits<std::uint32_t>::max();
    constexpr std::uint32_t kStreamCount = 1;
    const RecCodecFacts& facts = rec_codec_facts(f.codec);
    Header out{};
    Put p{out};
    const std::uint32_t scale = f.scale != 0 ? f.scale
                                             : FrameTicks::k60Hz().dw_scale(RecEvery::one());
    const std::uint32_t rate = f.rate != 0 ? f.rate : FrameTicks::kTickHz;
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
    p.u32(f.rows);
    p.at += kAvihReservedBytes;
    p.cc("LIST");
    p.u32(kStrl);
    p.cc("strl");
    p.cc("strh");
    p.u32(kStrhBytes);
    p.cc("vids");
    p.cc(facts.fourcc);
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
    p.u16(f.rows);
    p.cc("strf");
    p.u32(kStrfBytes);
    p.u32(kStrfBytes);
    p.u32(f.width);
    p.u32(f.rows);
    p.u16(1);
    p.u16(facts.bits_per_pixel);
    p.cc(facts.fourcc);
    const std::size_t image_bytes = rows_span(
        PictureSize{f.width, f.rows}.stride_bytes(facts.bytes_per_pixel, facts.row_align),
        f.rows
    );
    p.u32(static_cast<std::uint32_t>(image_bytes));
    p.at += kStrfTrailBytes;
    p.cc("JUNK");
    p.u32(kMoviListAt - kJunkAt - kChunkHeadBytes);
    p.at = kMoviListAt;
    p.cc("LIST");
    p.u32(clamp32(kListTypeBytes + f.movi_bytes));
    p.cc("movi");
    return out;
}

void chunk_head(std::uint32_t payload, std::span<std::byte, kChunkHead> out) noexcept {
    Put p{out};
    p.cc("00dc");
    p.u32(payload);
}

void index_head(AviFrameCount entries, std::span<std::byte, kChunkHead> out) noexcept {
    Put p{out};
    p.cc("idx1");
    p.u32(static_cast<std::uint32_t>(entries.scaled_by(kIndexEntry)));
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
