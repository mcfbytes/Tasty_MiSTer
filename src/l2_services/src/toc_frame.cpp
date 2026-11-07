// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/toc_frame.h"

#include <algorithm>
#include <span>
#include <utility>

#include "infra/endian.h"
#include "svc/msf.h"

namespace mister::svc {

namespace {

std::uint32_t msf_word(Lba lba) noexcept {
    constexpr unsigned kMinuteShift = 8;
    const Msf pos = Msf::of_lba_bcd_masked(lba.v);
    return (std::uint32_t{pos.m} << kMinuteShift) | pos.s;
}

struct FrameWriter {
    std::array<std::byte, kTocFrameBytes> bytes{};

    void u16(std::size_t off, std::uint16_t v) noexcept {
        infra::store_le<std::uint16_t>(std::span(bytes).subspan(off).first<sizeof(v)>(), v);
    }

    void u32(std::size_t off, std::uint32_t v) noexcept {
        infra::store_le<std::uint32_t>(std::span(bytes).subspan(off).first<sizeof(v)>(), v);
    }
};

}

std::array<std::byte, kTocFrameBytes> build_toc_frame(
    const Toc& toc,
    const TocFrameMeta& meta
) noexcept {

    constexpr std::size_t kOffDiscEnd = 4;
    constexpr std::size_t kOffDiscEndMsf = 8;
    constexpr std::size_t kOffLibcrypt = 12;
    constexpr std::size_t kOffMeta = 14;
    constexpr std::size_t kHeaderBytes = 16;
    constexpr std::size_t kRowBytes = 16;
    constexpr std::size_t kRowOffEnd = 4;
    constexpr std::size_t kRowOffMsf = 8;
    constexpr std::uint16_t kMetaResetBit = 0x4u;
    constexpr unsigned kAudioFlagShift = 16;
    constexpr std::uint32_t kInclusiveEnd = 1;
    constexpr std::uint32_t kFrameTracks = kTocFrameTracks;

    FrameWriter w;

    const TrackCount last = std::min(toc.last, TrackCount{kFrameTracks});
    w.u32(0, last.bcd_over_binary_word());
    w.u32(kOffDiscEnd, toc.end.v);
    w.u32(kOffDiscEndMsf, msf_word(toc.end));
    w.u16(kOffLibcrypt, meta.libcrypt_mask);
    std::uint16_t md = std::to_underlying(meta.region);
    if (meta.reset_request)
        md |= kMetaResetBit;
    w.u16(kOffMeta, md);

    last.for_each_index([&](TrackIndex i) {
        const Track& t = toc.at(i);
        const std::size_t base = kHeaderBytes + i.row_offset(kRowBytes);
        const Lba start_sent = (i != TrackIndex{}) ? t.start : Lba{};
        w.u32(base, start_sent.v);
        w.u32(base + kRowOffEnd, t.end.v - kInclusiveEnd);
        std::uint32_t bcd = msf_word(start_sent + t.pregap);
        if (t.type == TrackType::Cdda)
            bcd |= 1u << kAudioFlagShift;
        w.u32(base + kRowOffMsf, bcd);
    });
    return w.bytes;
}

}
