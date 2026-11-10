// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_block_decl.h"
#include "proto/block_geometry.h"
#include "proto/lane_block_decl.h"
#include "proto/osd_block_decl.h"
#include "proto/sector_block_decl.h"
#include "proto/sys_block_decl.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

#include "hal/osd_block.h"
#include "hal/uio_frame_lane.h"
#include "hal/sector_block.h"
#include "hal/sys_block.h"
#include "hal/types.h"
#include "hal/uio_reg_file.h"
#include "os/uio_line_space.h"
#include "proto/irq_binding.h"
#include "proto/uio_opcodes.h"
#include "reactor/notifier.h"

namespace mister::proto {

namespace {

template <class E>
    requires std::is_enum_v<E>
[[nodiscard]] constexpr auto ops_word(E word) noexcept {
    return std::to_underlying(word);
}

namespace sector_ops {
enum class SectorWord : std::size_t {
    Shape = 0,
    Cause = 1,
    Fifo = 2,
    CardsFirst = 3,
    CardWsrcFirst = 4,
    CardExtentFirst = 7,
    CardStride = 2,
};

enum class CardFlag : std::uint8_t { Read = 1u, Write = 2u };
}

namespace osd_ops {
enum class OsdWord : std::size_t { Shape = 0 };
}

namespace sys_ops {
enum class SysWord : std::size_t {
    Ordinal = 0,
    Cause = 1,
    Tag = 2,
    Causes = 3,
};
}

namespace lane_ops {

enum class LaneOp : std::size_t { Caps = 8, Fifo = 9 };
}

static_assert(
    ops_word(lane_ops::LaneOp::Caps) == 8 && ops_word(lane_ops::LaneOp::Fifo) == 9,
    "the lane entry's ops words are the ones lw's directory declares"
);

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

    [[nodiscard]] constexpr bool flags_bit(std::uint8_t bit) const noexcept {
        return (flags_byte() & bit) != 0;
    }
};

namespace sector_ops {

[[nodiscard]] constexpr bool carries(const DirWord& word, CardFlag flag) noexcept {
    return word.flags_bit(std::to_underlying(flag));
}
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
    const hal::LwOffset offset{d.offset};
    return offset.dword_aligned()
        && d.size >= need
        && d.offset >= hal::SysDirectory::kSize
        && offset.fits(window_size, need);
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
    const DirWord shape{d->ops[ops_word(sector_ops::SectorWord::Shape)]};
    const DirWord fifo{d->ops[ops_word(sector_ops::SectorWord::Fifo)]};

    if (shape.top_byte() >= kBlockSlots) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), shape.top_byte()});
    }
    const SectorBlockDecl s{
        .offset = d->offset,
        .size = d->size,
        .slot = SlotIndex{shape.top_byte()},
        .frame_words = shape.low16(),
        .cause = d->ops[ops_word(sector_ops::SectorWord::Cause)],
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
        const DirWord card{d->ops
                               [ops_word(sector_ops::SectorWord::CardsFirst)
                                + ops_word(sector_ops::SectorWord::CardStride) * c]};
        out.cards[c] = SectorBlockDecl::Card{
            .slot = SlotIndex{card.top_byte()},
            .reads = sector_ops::carries(card, sector_ops::CardFlag::Read),
            .writes = sector_ops::carries(card, sector_ops::CardFlag::Write),
            .frame_words = card.low16(),
            .wsrc = d->ops
                        [ops_word(sector_ops::SectorWord::CardWsrcFirst)
                         + ops_word(sector_ops::SectorWord::CardStride) * c],
            .extent = d->ops[ops_word(sector_ops::SectorWord::CardExtentFirst) + c]
        };

        if (out.cards[c].declared() && out.cards[c].slot.v >= kBlockSlots) {
            return std::unexpected(Error{Errc::slot_range, ERR_SITE(), out.cards[c].slot.v});
        }
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
    if (d->ops[ops_word(osd_ops::OsdWord::Shape)] != hal::OsdBlock::kGeometry) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), d->ops[ops_word(osd_ops::OsdWord::Shape)]}
        );
    }
    return OsdBlockDecl{
        .offset = d->offset,
        .size = d->size,
        .geometry = d->ops[ops_word(osd_ops::OsdWord::Shape)]
    };
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

    const std::uint32_t ordinal = d->ops[ops_word(sys_ops::SysWord::Ordinal)];
    if (ordinal < kIrqPoolFirstLine || ordinal > kIrqOrdinalMax) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), ordinal});
    }

    const std::uint32_t tag = d->ops[ops_word(sys_ops::SysWord::Tag)];
    if (tag != reactor::cause_word::kSys) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), tag});
    }
    const std::uint32_t causes = d->ops[ops_word(sys_ops::SysWord::Causes)];
    if ((causes & hal::SysBlock::kCauseStatus) == 0u) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), causes});
    }
    return SysBlockDecl{
        .offset = d->offset,
        .size = d->size,
        .line = os::UioLineSpace::line_at(ordinal, kIrqPoolFirstLine),
        .cause = d->ops[ops_word(sys_ops::SysWord::Cause)],
        .tag = tag,
        .causes = causes,
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
    out.caps = d->ops[ops_word(lane_ops::LaneOp::Caps)];
    out.fifo_depth = DirWord{d->ops[ops_word(lane_ops::LaneOp::Fifo)]}.high16();
    out.strobe_gap = DirWord{d->ops[ops_word(lane_ops::LaneOp::Fifo)]}.low16();
    out.line_decl = d->line;
    return out;
}

}
