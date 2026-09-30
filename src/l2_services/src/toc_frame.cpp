// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/toc_frame.h"

#include <algorithm>

namespace mister::svc {

namespace {

constexpr std::uint32_t kFramesPerSecond = 75;

std::uint32_t bcd8(std::uint32_t v) noexcept { return (((v / 10) % 10) << 4) | (v % 10); }

std::uint32_t msf_word(std::uint32_t lba) noexcept {
    const std::uint32_t sec = lba / kFramesPerSecond;
    return (bcd8(sec / 60) << 8) | bcd8(sec % 60);
}

struct FrameWriter {
    std::array<std::byte, kTocFrameBytes> bytes{};

    void u16(std::size_t off, std::uint16_t v) noexcept {
        bytes[off] = static_cast<std::byte>(v & 0xFFu);
        bytes[off + 1] = static_cast<std::byte>((v >> 8) & 0xFFu);
    }
    void u32(std::size_t off, std::uint32_t v) noexcept {
        u16(off, static_cast<std::uint16_t>(v & 0xFFFFu));
        u16(off + 2, static_cast<std::uint16_t>(v >> 16));
    }
};

}  // namespace

std::array<std::byte, kTocFrameBytes> build_toc_frame(const Toc& toc,
                                                      const TocFrameMeta& meta) noexcept {
    FrameWriter w;

    const std::uint32_t last =
        std::min<std::uint32_t>(toc.last, static_cast<std::uint32_t>(kTocFrameTracks));
    w.u32(0, (bcd8(last) << 8) | last);
    w.u32(4, toc.end.v);
    w.u32(8, msf_word(toc.end.v));
    w.u16(12, meta.libcrypt_mask);
    std::uint16_t md = static_cast<std::uint16_t>(meta.region);
    if (meta.reset_request) md |= 0x4u;
    w.u16(14, md);

    for (std::uint32_t i = 0; i < last; ++i) {
        const Track& t = toc.tracks[i];
        const std::size_t base = 16 + std::size_t{16} * i;
        const std::uint32_t start_sent = (i != 0) ? t.start.v : 0;
        w.u32(base + 0, start_sent);
        w.u32(base + 4, t.end.v - 1);
        std::uint32_t bcd = msf_word(start_sent + t.pregap);
        if (t.type == TrackType::Cdda) bcd |= 1u << 16;
        w.u32(base + 8, bcd);
    }
    return w.bytes;
}

}  // namespace mister::svc
