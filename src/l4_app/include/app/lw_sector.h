// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "cores/blank_save_spec.h"
#include "hal/phys_region.h"
#include "hal/register_window.h"
#include "hal/sector_block.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/uio_handle.h"
#include "proto/sector_block_decl.h"
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
    std::uint32_t fabric_gen = 0;
    bool fabric_lost = false;

    [[nodiscard]] std::uint16_t card_slots() const noexcept {
        std::uint16_t m = 0;
        for (const Card& c : cards)
            if (c.image.len != 0)
                m = static_cast<std::uint16_t>(m | (1u << (c.decl.slot & 15u)));
        return m;
    }

    [[nodiscard]] std::uint16_t bridge_slots() const noexcept {
        return static_cast<std::uint16_t>(card_slots() | (1u << (block.slot & 15u)));
    }
};

}
