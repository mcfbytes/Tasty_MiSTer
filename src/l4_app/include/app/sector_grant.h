// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "cores/blank_save_spec.h"
#include "hal/phys_region.h"
#include "hal/reg_lend.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/uio_line_ref.h"
#include "svc/sector_program.h"

namespace mister::app {

struct SectorCard {
    std::uint8_t slot = 0;
    std::uint16_t frame_words = 0;
    hal::PhysRegion image{};
};

struct SectorGrant {
    static constexpr std::size_t kCards = 2;
    hal::reg::Lend block{};
    os::UioLineRef line{};
    infra::OptRef<const svc::ISectorProgram> program{};
    std::uint16_t gen = 0;
    std::uint16_t frame_words = 0;
    std::array<SectorCard, kCards> cards{};
    infra::OptRef<const cores::BlankSaveSpec> blank{};

    [[nodiscard]] bool live() const noexcept { return block.lent() && program.has_value(); }
};

static_assert(std::is_trivially_copyable_v<SectorGrant>);
using SectorGrantCell = xthread::Telemetry<SectorGrant, SeatTag::Main>;

}
