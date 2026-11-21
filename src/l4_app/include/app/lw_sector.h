// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "app/lw_grant_stamp.h"
#include "app/sector_grant.h"
#include "app/types.h"
#include "hal/phys_region.h"
#include "hal/register_window.h"
#include "hal/sector_block.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/link/sector_block_decl.h"
#include "proto/types.h"
#include "svc/disc/sector_program.h"

namespace mister::app {

struct LwSector {
    TASTY_SEAT_EXEMPT(component);
    using Grant = SectorGrant;

    struct Card {
        proto::link::SectorBlockDecl::Card decl{};
        std::optional<hal::ApertureRegion> image{};
    };

    hal::RegisterWindow<hal::SectorBlock> regs;
    os::UioHandle line{};
    proto::link::SectorBlockDecl block{};
    infra::OptRef<const svc::disc::ISectorProgram> program{};

    std::array<Card, proto::link::SectorBlockDecl::kCards> cards{};
    LwGrantStamp stamp{};

    [[nodiscard]] proto::BlockSlotMask card_slots() const noexcept {
        proto::BlockSlotMask m{};
        for (const Card& c : cards)
            if (c.image)
                m = m.with(c.decl.slot);
        return m;
    }

    [[nodiscard]] proto::BlockSlotMask bridge_slots() const noexcept {
        return card_slots().with(block.slot);
    }

    [[nodiscard]] Grant lend() const noexcept {
        Grant g{
            {.block = regs.lend(),
             .gen = stamp.gen(),
             .body = {.line = line.borrow(), .program = program, .frame_words = block.frame_words}}
        };
        for (std::size_t c = 0; c < g.body.cards.size(); ++c) {
            const Card& card = cards[c];
            if (!card.image)
                continue;
            g.body.cards[c] = SectorCard{
                .index = card.decl.index,
                .slot = card.decl.slot,
                .frame_words = card.decl.frame_words,
                .image = card.image
            };
        }
        return g;
    }
};

}
