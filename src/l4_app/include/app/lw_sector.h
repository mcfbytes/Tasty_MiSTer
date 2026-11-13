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
#include "proto/sector_block_decl.h"
#include "proto/types.h"
#include "svc/sector_program.h"

namespace mister::app {

struct LwSector {
    TASTY_SEAT_EXEMPT(component);
    using Grant = SectorGrant;

    static constexpr bool kHasEnable = true;

    struct Card {
        proto::SectorBlockDecl::Card decl{};
        hal::PhysRegion image{};
    };

    hal::RegisterWindow<hal::SectorBlock> regs;
    os::UioHandle line{};
    proto::SectorBlockDecl block{};
    infra::OptRef<const svc::ISectorProgram> program{};
    std::array<Card, proto::SectorBlockDecl::kCards> cards{};
    LwGrantStamp stamp{};

    [[nodiscard]] proto::BlockSlotMask card_slots() const noexcept {
        proto::BlockSlotMask m{};
        for (const Card& c : cards)
            if (c.image.len != 0)
                m = m.with(c.decl.slot);
        return m;
    }

    [[nodiscard]] proto::BlockSlotMask bridge_slots() const noexcept {
        return card_slots().with(block.slot);
    }

    [[nodiscard]] Grant lend() const noexcept {
        Grant g{
            .block = regs.lend(),
            .line = line.borrow(),
            .program = program,
            .gen = stamp.gen(),
            .frame_words = block.frame_words
        };
        for (std::size_t c = 0; c < g.cards.size(); ++c) {
            const Card& card = cards[c];
            if (card.image.len == 0)
                continue;
            g.cards[c] = SectorCard{
                .slot = card.decl.slot,
                .frame_words = card.decl.frame_words,
                .image = card.image
            };
        }
        return g;
    }

    void disable() noexcept {
        regs.write(hal::SectorBlock::Reg::Ctrl, 0);
        (void)regs.read(hal::SectorBlock::Reg::Ctrl);
    }
};

}
