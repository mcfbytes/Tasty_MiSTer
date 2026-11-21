// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/disc/toc_frame.h"

#include <algorithm>
#include <span>
#include <utility>

#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "svc/msf.h"
#include "svc/track_row.h"

namespace mister::svc::disc {

namespace {

std::uint32_t msf_word(Lba lba) noexcept {
    constexpr unsigned kMinuteShift = 8;
    const Msf pos = Msf::of_lba_bcd_masked(lba);
    return (std::uint32_t{pos.m} << kMinuteShift) | pos.s;
}

struct FrameWriter {
    std::array<std::byte, kTocFrameBytes> bytes{};

    void u16(infra::ByteOffset off, std::uint16_t v) noexcept {
        infra::store_le<std::uint16_t>(
            infra::affine::from(std::span(bytes), off).first<sizeof(v)>(),
            v
        );
    }

    void u32(infra::ByteOffset off, std::uint32_t v) noexcept {
        infra::store_le<std::uint32_t>(
            infra::affine::from(std::span(bytes), off).first<sizeof(v)>(),
            v
        );
    }

    void lba(infra::ByteOffset off, Lba v) noexcept {
        v.store_le32(infra::affine::from(std::span(bytes), off).first<sizeof(std::uint32_t)>());
    }
};

}

std::array<std::byte, kTocFrameBytes> build_toc_frame(
    const Toc& toc,
    const TocFrameMeta& meta
) noexcept {

    constexpr infra::ByteOffset kOffDiscEnd{4};
    constexpr infra::ByteOffset kOffDiscEndMsf{8};
    constexpr infra::ByteOffset kOffLibcrypt{12};
    constexpr infra::ByteOffset kOffMeta{14};
    constexpr infra::ByteOffset kHeaderBytes{16};
    constexpr xthread::SlotStride kRowBytes{16};
    constexpr infra::ByteCount kRowOffEnd{4};
    constexpr infra::ByteCount kRowOffMsf{8};
    constexpr infra::ByteOffset kRowTableOrigin{};
    constexpr std::uint16_t kMetaResetBit = 0x4u;
    constexpr unsigned kAudioFlagShift = 16;
    constexpr proto::BlockCount kInclusiveEnd{1};
    constexpr std::uint32_t kFrameTracks = kTocFrameTracks;

    FrameWriter w;

    const TrackCount last = std::min(toc.last, TrackCount{kFrameTracks});
    w.u32(infra::ByteOffset{}, last.bcd_over_binary_word());
    w.lba(kOffDiscEnd, toc.end);
    w.u32(kOffDiscEndMsf, msf_word(toc.end));
    w.u16(kOffLibcrypt, meta.libcrypt_mask);
    std::uint16_t md = std::to_underlying(meta.region);
    if (meta.reset_request)
        md |= kMetaResetBit;
    w.u16(kOffMeta, md);

    last.for_each_index([&](TrackIndex i) {
        const Track& t = toc.tracks[i];
        const infra::ByteCount row_at = i.row_offset(kRowBytes) - kRowTableOrigin;
        const infra::ByteOffset base = kHeaderBytes + row_at;
        const Lba start_sent = (i != TrackIndex{}) ? t.start : Lba{};
        w.lba(base, start_sent);
        w.lba(base + kRowOffEnd, t.end - kInclusiveEnd);
        std::uint32_t bcd = msf_word(start_sent + t.pregap);
        if (t.type == TrackType::Cdda)
            bcd |= 1u << kAudioFlagShift;
        w.u32(base + kRowOffMsf, bcd);
    });
    return w.bytes;
}

}
