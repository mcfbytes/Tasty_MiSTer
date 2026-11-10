// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "app/types.h"
#include "cores/blank_save_spec.h"
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

    struct Card {
        proto::SectorBlockDecl::Card decl{};
        hal::PhysRegion image{};
    };

    hal::RegisterWindow<hal::SectorBlock> regs;
    os::UioHandle line{};
    proto::SectorBlockDecl block{};
    infra::OptRef<const svc::ISectorProgram> program{};
    std::array<Card, proto::SectorBlockDecl::kCards> cards{};
    infra::OptRef<const cores::BlankSaveSpec> blank{};
    std::uint16_t gen = 0;
    FabricGeneration fabric_gen{};
    bool fabric_lost = false;

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
};

}
