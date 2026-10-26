// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_block_decl.h"
#include "proto/lane_block_decl.h"
#include "proto/osd_block_decl.h"
#include "proto/sector_block_decl.h"
#include "proto/sys_block_decl.h"

#include <algorithm>
#include <cstddef>

#include "hal/osd_block.h"
#include "hal/uio_frame_lane.h"
#include "hal/sector_block.h"
#include "hal/sys_block.h"
#include "hal/uio_reg_file.h"
#include "proto/irq_binding.h"
#include "reactor/notifier.h"

namespace mister::proto {

namespace {

constexpr std::uint16_t kJoystick0 = 0x02;
constexpr std::uint16_t kJoystick1 = 0x03;

AxiBlockDecl read_entry(const SysDirectoryImage& dir, std::uint32_t i) {
    AxiBlockDecl d{};
    d.kind = dir.entry_word(i, 0);
    d.version = dir.entry_word(i, 1);
    d.offset = dir.entry_word(i, 2);
    d.size = dir.entry_word(i, 3);
    for (std::uint32_t w = 0; w < d.ops.size(); ++w)
        d.ops[w] = dir.entry_word(i, 4 + w);
    d.line = dir.line(i);
    return d;
}

[[nodiscard]] Ex<AxiBlockDecl> find_entry(
    const SysDirectoryImage& dir,
    std::uint32_t kind,
    std::uint32_t major
) noexcept {
    using hal::SysDirectory;
    const std::uint32_t magic = dir.magic();
    if (magic != SysDirectory::kMagic) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), magic});
    }
    const std::uint32_t version = dir.version();
    if ((version >> 16) != SysDirectory::kMajor) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), version});
    }
    const std::uint32_t n = dir.count();
    for (std::uint32_t i = 0; i < n; ++i) {
        const AxiBlockDecl d = read_entry(dir, i);
        if (d.kind != kind)
            continue;
        if ((d.version >> 16) != major) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), d.version});
        }
        return d;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), n});
}

bool block_fits(const AxiBlockDecl& d, std::size_t need, std::uint32_t window_size) noexcept {
    return (d.offset & 3u) == 0u
        && d.size >= need
        && d.offset >= hal::SysDirectory::kSize
        && d.offset <= window_size
        && need <= window_size - d.offset;
}

}

[[nodiscard]] Ex<AxiBlockDecl> find_pad_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept {
    auto d = find_entry(dir, AxiBlockDecl::kPadKind, AxiBlockDecl::kPadMajor);
    if (!d)
        return d;
    if (!block_fits(*d, hal::UioRegFile::kSize, window_size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), d->offset});
    }
    for (const std::uint16_t op : {kJoystick0, kJoystick1}) {
        if (!d->honours(op))
            return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), op});
    }
    return d;
}

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept {
    auto d = find_entry(dir, AxiBlockDecl::kSectorKind, AxiBlockDecl::kSectorMajor);
    if (!d)
        return std::unexpected(d.error());
    if (!block_fits(*d, hal::SectorBlock::kSize, window_size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), d->offset});
    }
    const std::uint32_t shape = d->ops[0];
    const std::uint32_t fifo = d->ops[2];
    const SectorBlockDecl s{
        .offset = d->offset,
        .size = d->size,
        .slot = static_cast<std::uint8_t>(shape >> 24),
        .frame_words = static_cast<std::uint16_t>(shape & 0xFFFFu),
        .cause = d->ops[1],
        .fifo_depth = static_cast<std::uint16_t>(fifo >> 16),
        .strobe_gap = static_cast<std::uint16_t>(fifo & 0xFFFFu)
    };
    if (s.frame_words == 0 || s.frame_words > hal::SectorBlock::kMaxFrameWords) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), s.frame_words});
    }
    SectorBlockDecl out = s;
    out.line_decl = d->line;
    out.minor = static_cast<std::uint16_t>(d->version & 0xFFFFu);
    if (out.minor == 0)
        return out;
    for (std::size_t c = 0; c < SectorBlockDecl::kCards; ++c) {
        const std::uint32_t card = d->ops[3 + 2 * c];
        out.cards[c] = SectorBlockDecl::Card{
            .slot = static_cast<std::uint8_t>(card >> 24),
            .flags = static_cast<std::uint8_t>((card >> 16) & 0xFFu),
            .frame_words = static_cast<std::uint16_t>(card & 0xFFFFu),
            .wsrc = d->ops[4 + 2 * c],
            .extent = d->ops[7 + c]
        };
    }
    return out;
}

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept {
    auto d = find_entry(dir, AxiBlockDecl::kOsdKind, AxiBlockDecl::kOsdMajor);
    if (!d)
        return std::unexpected(d.error());
    if (!block_fits(*d, hal::OsdBlock::kSize, window_size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), d->offset});
    }
    if (d->ops[0] != hal::OsdBlock::kGeometry) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d->ops[0]});
    }
    return OsdBlockDecl{.offset = d->offset, .size = d->size, .geometry = d->ops[0]};
}

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept {
    auto d = find_entry(dir, AxiBlockDecl::kSysKind, AxiBlockDecl::kSysMajor);
    if (!d)
        return std::unexpected(d.error());
    if (!block_fits(*d, hal::SysBlock::kSize, window_size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), d->offset});
    }

    const std::uint32_t ordinal = d->ops[0];
    if (ordinal < kIrqPoolFirstLine || ordinal > kIrqOrdinalMax) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), ordinal});
    }

    if (d->ops[2] != reactor::cause_word::kSys) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d->ops[2]});
    }
    if ((d->ops[3] & hal::SysBlock::kCauseStatus) == 0u) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), d->ops[3]});
    }
    return SysBlockDecl{
        .offset = d->offset,
        .size = d->size,
        .line = static_cast<os::UioLine>(ordinal - kIrqPoolFirstLine),
        .cause = d->ops[1],
        .tag = d->ops[2],
        .causes = d->ops[3],
        .line_decl = d->line
    };
}

[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const SysDirectoryImage& dir,
    std::uint32_t window_size
) noexcept {
    auto d = find_entry(dir, LaneBlockDecl::kKind, LaneBlockDecl::kMajor);
    if (!d)
        return std::unexpected(d.error());
    if (!block_fits(*d, hal::UioFrameLane::kSize, window_size)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), d->offset});
    }
    LaneBlockDecl out{.offset = d->offset, .size = d->size};
    std::copy_n(d->ops.begin(), out.ops.size(), out.ops.begin());
    out.caps = d->ops[8];
    out.fifo_depth = static_cast<std::uint16_t>(d->ops[9] >> 16);
    out.strobe_gap = static_cast<std::uint16_t>(d->ops[9] & 0xFFFFu);
    out.line_decl = d->line;
    return out;
}

[[nodiscard]] Ex<AxiBlockDecl> find_pad_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept {
    return find_pad_block(SysDirectoryImage::read(dir), window_size);
}

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept {
    return find_sector_block(SysDirectoryImage::read(dir), window_size);
}

[[nodiscard]] Ex<OsdBlockDecl> find_osd_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept {
    return find_osd_block(SysDirectoryImage::read(dir), window_size);
}

[[nodiscard]] Ex<SysBlockDecl> find_sys_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept {
    return find_sys_block(SysDirectoryImage::read(dir), window_size);
}

[[nodiscard]] Ex<LaneBlockDecl> find_lane_block(
    const hal::RegisterWindow<hal::SysDirectory>& dir,
    std::uint32_t window_size
) noexcept {
    return find_lane_block(SysDirectoryImage::read(dir), window_size);
}

}
