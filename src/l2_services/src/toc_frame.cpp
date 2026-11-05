// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/toc_frame.h"

#include <algorithm>
#include <span>
#include <utility>

#include "infra/endian.h"

namespace mister::svc {

namespace {

constexpr std::uint32_t kFramesPerSecond = 75;

std::uint32_t bcd8(std::uint32_t v) noexcept {
    constexpr std::uint32_t kRadix = 10;
    constexpr unsigned kNibbleBits = 4;
    return (((v / kRadix) % kRadix) << kNibbleBits) | (v % kRadix);
}

std::uint32_t msf_word(Lba lba) noexcept {
    constexpr std::uint32_t kSecondsPerMinute = 60;
    constexpr unsigned kMinuteShift = 8;
    const std::uint32_t sec = lba.v / kFramesPerSecond;
    return (bcd8(sec / kSecondsPerMinute) << kMinuteShift) | bcd8(sec % kSecondsPerMinute);
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
    constexpr unsigned kBcdShift = 8;
    constexpr std::uint32_t kInclusiveEnd = 1;
    constexpr std::uint32_t kFrameTracks = kTocFrameTracks;

    FrameWriter w;

    const std::uint32_t last = std::min<std::uint32_t>(toc.last, kFrameTracks);
    w.u32(0, (bcd8(last) << kBcdShift) | last);
    w.u32(kOffDiscEnd, toc.end.v);
    w.u32(kOffDiscEndMsf, msf_word(toc.end));
    w.u16(kOffLibcrypt, meta.libcrypt_mask);
    std::uint16_t md = std::to_underlying(meta.region);
    if (meta.reset_request)
        md |= kMetaResetBit;
    w.u16(kOffMeta, md);

    for (std::uint32_t i = 0; i < last; ++i) {
        const Track& t = toc.tracks[i];
        const std::size_t base = kHeaderBytes + kRowBytes * i;
        const Lba start_sent = (i != 0) ? t.start : Lba{};
        w.u32(base, start_sent.v);
        w.u32(base + kRowOffEnd, t.end.v - kInclusiveEnd);
        std::uint32_t bcd = msf_word(start_sent + t.pregap);
        if (t.type == TrackType::Cdda)
            bcd |= 1u << kAudioFlagShift;
        w.u32(base + kRowOffMsf, bcd);
    }
    return w.bytes;
}

}
