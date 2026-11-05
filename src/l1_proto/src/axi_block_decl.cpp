// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_block_decl.h"
#include "proto/lane_block_decl.h"
#include "proto/osd_block_decl.h"
#include "proto/sector_block_decl.h"
#include "proto/sys_block_decl.h"

#include <algorithm>
#include <cstddef>
#include <limits>

#include "hal/osd_block.h"
#include "hal/uio_frame_lane.h"
#include "hal/sector_block.h"
#include "hal/sys_block.h"
#include "hal/uio_reg_file.h"
#include "proto/irq_binding.h"
#include "proto/uio_opcodes.h"
#include "reactor/notifier.h"

namespace mister::proto {

namespace {

enum OpsWord : std::size_t {
    kShapeWord = 0,
    kCauseWord = 1,
    kFifoWord = 2,
    kCardWordsFirst = 3,
    kCardWsrcFirst = 4,
    kCardExtentFirst = 7,
    kCardStride = 2,
    kLaneCapsWord = 8,
    kLaneFifoWord = 9,
    kSysOrdinalWord = 0,
    kSysTagWord = 2,
    kSysCausesWord = 3,
};

enum : std::uint32_t { kAlignMask = 3 };

struct DirWord {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint16_t low16() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    [[nodiscard]] constexpr std::uint16_t high16() const noexcept {
        return static_cast<std::uint16_t>(v >> std::numeric_limits<std::uint16_t>::digits);
    }

    [[nodiscard]] constexpr std::uint8_t top_byte() const noexcept {
        return static_cast<std::uint8_t>(
            v
            >> (std::numeric_limits<std::uint32_t>::digits
                - std::numeric_limits<std::uint8_t>::digits)
        );
    }

    [[nodiscard]] constexpr std::uint8_t flags_byte() const noexcept {
        return static_cast<std::uint8_t>(
            (v >> std::numeric_limits<std::uint16_t>::digits)
            & std::numeric_limits<std::uint8_t>::max()
        );
    }
};

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
    if (DirWord{version}.high16() != SysDirectory::kMajor) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), version});
    }
    const std::uint32_t n = dir.count();
    for (std::uint32_t i = 0; i < n; ++i) {
        const AxiBlockDecl d = dir.entry(i);
        if (d.kind != kind)
            continue;
        if (DirWord{d.version}.high16() != major) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), d.version});
        }
        return d;
    }
    return std::unexpected(Error{Errc::not_found, ERR_SITE(), n});
}

bool block_fits(const AxiBlockDecl& d, std::size_t need, std::uint32_t window_size) noexcept {
    return (d.offset & kAlignMask) == 0u
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
    for (const UioOp op : {UioOp::Joystick0, UioOp::Joystick1}) {
        if (!d->honours(op)) {
            return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), proto::word(op).v});
        }
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
    const DirWord shape{d->ops[kShapeWord]};
    const DirWord fifo{d->ops[kFifoWord]};
    const SectorBlockDecl s{
        .offset = d->offset,
        .size = d->size,
        .slot = SlotIndex{shape.top_byte()},
        .frame_words = shape.low16(),
        .cause = d->ops[kCauseWord],
        .fifo_depth = fifo.high16(),
        .strobe_gap = fifo.low16()
    };
    if (s.frame_words == 0 || s.frame_words > hal::SectorBlock::kMaxFrameWords) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), s.frame_words});
    }
    SectorBlockDecl out = s;
    out.line_decl = d->line;
    out.minor = DirWord{d->version}.low16();
    if (out.minor == 0)
        return out;
    for (std::size_t c = 0; c < SectorBlockDecl::kCards; ++c) {
        const DirWord card{d->ops[kCardWordsFirst + kCardStride * c]};
        out.cards[c] = SectorBlockDecl::Card{
            .slot = SlotIndex{card.top_byte()},
            .flags = card.flags_byte(),
            .frame_words = card.low16(),
            .wsrc = d->ops[kCardWsrcFirst + kCardStride * c],
            .extent = d->ops[kCardExtentFirst + c]
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
    if (d->ops[kShapeWord] != hal::OsdBlock::kGeometry) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d->ops[kShapeWord]});
    }
    return OsdBlockDecl{.offset = d->offset, .size = d->size, .geometry = d->ops[kShapeWord]};
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

    const std::uint32_t ordinal = d->ops[kSysOrdinalWord];
    if (ordinal < kIrqPoolFirstLine || ordinal > kIrqOrdinalMax) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), ordinal});
    }

    if (d->ops[kSysTagWord] != reactor::cause_word::kSys) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d->ops[kSysTagWord]});
    }
    if ((d->ops[kSysCausesWord] & hal::SysBlock::kCauseStatus) == 0u) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), d->ops[kSysCausesWord]});
    }
    return SysBlockDecl{
        .offset = d->offset,
        .size = d->size,
        .line = static_cast<os::UioLine>(ordinal - kIrqPoolFirstLine),
        .cause = d->ops[kCauseWord],
        .tag = d->ops[kSysTagWord],
        .causes = d->ops[kSysCausesWord],
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
    out.caps = d->ops[kLaneCapsWord];
    out.fifo_depth = DirWord{d->ops[kLaneFifoWord]}.high16();
    out.strobe_gap = DirWord{d->ops[kLaneFifoWord]}.low16();
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
