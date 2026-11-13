// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/sector_block_decl.h"

#include <cstddef>
#include <utility>

#include "hal/sector_block.h"
#include "kinded_dir_entry.h"
#include "proto/block_geometry.h"

namespace mister::proto {

namespace {

enum class SectorWord : std::size_t {
    Shape = 0,
    Cause = 1,
    Fifo = 2,
    Card0Shape = 3,
    Card0Wsrc = 4,
    Card1Shape = 5,
    Card1Wsrc = 6,
    Card0Extent = 7,
    Card1Extent = 8,
};

enum class CardFlag : std::uint8_t { Read = 1u, Write = 2u };

struct CardWords {
    SectorWord shape;
    SectorWord wsrc;
    SectorWord extent;
};

constexpr CardWords kCardWords[SectorBlockDecl::kCards] = {
    {SectorWord::Card0Shape, SectorWord::Card0Wsrc, SectorWord::Card0Extent},
    {SectorWord::Card1Shape, SectorWord::Card1Wsrc, SectorWord::Card1Extent}
};

static_assert(
    ops_word(kCardWords[0].shape) == 3
        && ops_word(kCardWords[0].wsrc) == 4
        && ops_word(kCardWords[0].extent) == 7,
    "card 0 is the tip's 3 + 2c, 4 + 2c and 7 + c"
);
static_assert(
    ops_word(kCardWords[1].shape) == 5
        && ops_word(kCardWords[1].wsrc) == 6
        && ops_word(kCardWords[1].extent) == 8,
    "card 1 is the tip's 3 + 2c, 4 + 2c and 7 + c"
);

constexpr DirEntryFacts kFacts{
    DirEntryKind::Sector,
    DirMajor::V1,
    hal::LwExtent::of_layout<hal::SectorBlock>()
};

[[nodiscard]] constexpr bool carries(DirWord word, CardFlag flag) noexcept {
    return word.flags_bit(ops_word(flag));
}

}

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const SysDirectoryImage& dir,
    hal::LwExtent window
) noexcept {
    auto e = find_kinded<SectorWord>(dir, kFacts, window);
    if (!e)
        return std::unexpected(e.error());
    const DirWord shape = e->word(SectorWord::Shape);
    const DirWord fifo = e->word(SectorWord::Fifo);

    if (shape.top_byte() >= kBlockSlots) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), shape.top_byte()});
    }
    const SectorBlockDecl s{
        .offset = e->offset(),
        .size = e->size(),
        .slot = SlotIndex{shape.top_byte()},
        .frame_words = shape.low16(),
        .cause = e->word(SectorWord::Cause).lw_offset(),
        .fifo_depth = fifo.high16(),
        .strobe_gap = fifo.low16()
    };
    if (s.frame_words == 0 || s.frame_words > hal::SectorBlock::kMaxFrameWords) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), s.frame_words});
    }
    SectorBlockDecl out = s;
    out.line_decl = e->line();
    out.version = e->version();
    if (out.version.is_base_minor())
        return out;
    for (std::size_t c = 0; c < SectorBlockDecl::kCards; ++c) {
        const DirWord card = e->word(kCardWords[c].shape);
        out.cards[c] = SectorBlockDecl::Card{
            .slot = SlotIndex{card.top_byte()},
            .reads = carries(card, CardFlag::Read),
            .writes = carries(card, CardFlag::Write),
            .frame_words = card.low16(),
            .wsrc = e->word(kCardWords[c].wsrc).phys_addr(),
            .extent = e->word(kCardWords[c].extent).map_length()
        };

        if (out.cards[c].declared() && !out.cards[c].slot.in_block_slots()) {
            return std::unexpected(Error{Errc::slot_range, ERR_SITE(), out.cards[c].slot.detail()});
        }
    }
    return out;
}

}
