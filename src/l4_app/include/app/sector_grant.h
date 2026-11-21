// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/block_grant.h"
#include "hal/aperture_region.h"
#include "hal/sector_block.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "os/uio_line_ref.h"
#include "proto/types.h"
#include "svc/disc/sector_program.h"

namespace mister::app {

struct SectorCard {
    hal::SectorBlock::Slot index{};
    proto::SlotIndex slot{};
    std::uint16_t frame_words = 0;
    std::optional<hal::ApertureRegion> image{};
};

struct SectorGrantBody {
    static constexpr std::size_t kCards = 2;
    os::UioLineRef line{};
    infra::OptRef<const svc::disc::ISectorProgram> program{};
    std::uint16_t frame_words = 0;
    std::array<SectorCard, kCards> cards{};

    [[nodiscard]] bool live() const noexcept { return program.has_value(); }
};

struct SectorGrant : BlockGrant<SectorGrantBody> {};

static_assert(BlockGrantRecord<SectorGrant>);
using SectorGrantCell = xthread::Telemetry<SectorGrant, SeatTag::Main>;

}
