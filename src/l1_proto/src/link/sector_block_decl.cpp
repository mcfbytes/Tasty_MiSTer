// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/sector_block_decl.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/sector_block.h"
#include "infra/prelude.h"
#include "kinded_dir_entry.h"
#include "proto/irq_binding.h"

namespace mister::proto::link {

namespace {

enum class SectorWord : std::size_t {
    Fifo = 0,
    Descriptors = 2,
    PrimaryShape = 4,
    Save0Shape = 8,
    Save0Lba = 9,
    Save0Wsrc = 10,
    Save0Extent = 11,
    Save1Shape = 12,
    Save1Lba = 13,
    Save1Wsrc = 14,
    Save1Extent = 15,
};

struct CardWords {
    SectorWord shape;
    SectorWord wsrc;
    SectorWord extent;
};

constexpr CardWords kCardWords[SectorBlockDecl::kCards] = {
    {SectorWord::Save0Shape, SectorWord::Save0Wsrc, SectorWord::Save0Extent},
    {SectorWord::Save1Shape, SectorWord::Save1Wsrc, SectorWord::Save1Extent}
};

static_assert(
    ops_word(SectorWord::PrimaryShape) == 4
        && ops_word(SectorWord::Save0Shape) == 8
        && ops_word(SectorWord::Save1Shape) == 12,
    "K4 + 4k: the primary at K4, the save slots at K8 and K12"
);
static_assert(
    ops_word(kCardWords[0].shape) == 8
        && ops_word(kCardWords[0].wsrc) == 10
        && ops_word(kCardWords[0].extent) == 11
        && ops_word(kCardWords[1].shape) == 12
        && ops_word(kCardWords[1].wsrc) == 14
        && ops_word(kCardWords[1].extent) == 15,
    "K4+4k shape, K6+4k wsrc, K7+4k extent"
);

enum class CardFlag : std::uint8_t { Read = 1u, Write = 2u };

constexpr DirEntryFacts kFacts{
    DirEntryKind::Sector,
    kKnownMajor(DirEntryKind::Sector),
    hal::LwExtent::of_layout<hal::SectorBlock>()
};

[[nodiscard]] constexpr bool carries(DirWord word, CardFlag flag) noexcept {
    return (word.top_byte() & ops_word(flag)) != 0u;
}

}

[[nodiscard]] Ex<SectorBlockDecl> find_sector_block(
    const SysDirectoryImage& dir,
    const AxiLwDecl& token
) noexcept {
    auto e = find_kinded<SectorWord>(dir, kFacts, token.size);
    if (!e)
        return std::unexpected(e.error());

    const IrqOrdinal ordinal = e->line().ordinal;
    if (!e->line().declared()
        || ordinal < IrqOrdinal{kIrqPoolFirstLine}
        || ordinal > IrqOrdinal{kIrqOrdinalMax}) {
        return std::unexpected(ordinal.refusal(Errc::slot_range, ERR_SITE()));
    }
    const DirWord count = e->word(SectorWord::Descriptors);
    if (count.low16() < SectorBlockDecl::kMinDescriptors
        || count.low16() > SectorBlockDecl::kMaxDescriptors) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), count.low16()});
    }
    const DirWord shape = e->word(SectorWord::PrimaryShape);
    if (shape.low16() == 0 || shape.low16() > hal::SectorBlock::kMaxFrameWords) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), shape.low16()});
    }

    if (!carries(shape, CardFlag::Read)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), shape.top_byte()});
    }
    const DirWord fifo = e->word(SectorWord::Fifo);
    SectorBlockDecl out{
        .offset = e->offset(),
        .size = e->size(),
        .last_descriptor = hal::SectorBlock::Slot::of(
            count.low16() - SectorBlockDecl::kMinDescriptors
        ),
        .slot = shape.hps_slot(),
        .reads = true,
        .frame_words = shape.low16(),
        .line =
            BlockLine{
                .line = ordinal.line_after(IrqOrdinal{kIrqPoolFirstLine}),
                .cause = token.offset.plus(e->cause())
            },
        .fifo_entries = fifo.high16(),
        .strobe_gap = fifo.low16(),
        .index = e->index()
    };
    out.version = e->version();

    for (std::size_t c = 0; c < SectorBlockDecl::kCards; ++c) {
        const hal::SectorBlock::Slot k = hal::SectorBlock::Slot::of(c + 1u);
        if (!(k <= out.last_descriptor))
            continue;
        const DirWord card = e->word(kCardWords[c].shape);
        out.cards[c] = SectorBlockDecl::Card{
            .index = k,
            .slot = card.hps_slot(),
            .reads = carries(card, CardFlag::Read),
            .writes = carries(card, CardFlag::Write),
            .frame_words = card.low16(),
            .wsrc = e->word(kCardWords[c].wsrc).phys_addr(),
            .extent = e->word(kCardWords[c].extent).map_length()
        };
    }
    return out;
}

static_assert(!CarriesOpBitmap<SectorWord>, "a SECTOR entry declares no ops bitmap");

}
