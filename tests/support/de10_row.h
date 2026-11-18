// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/boards_table.h"
#include "hal/link_timing.h"
#include "hal/program_geometry.h"

namespace mister::testing {

constexpr const hal::BoardProfile& kDe10Row() {
    return hal::board_by_id(hal::BoardId::De10Nano);
}

inline const hal::VideoOutDecl& de10_video() {
    return hal::board_by_id(hal::BoardId::De10Nano).video;
}

inline constexpr hal::LinkTimingValues kDe10LinkTiming = hal::LinkTimingValues::measured(
    hal::board_by_id(hal::BoardId::De10Nano).timing
);
inline constexpr std::uint32_t kDe10RoundBudgetWords = kDe10LinkTiming.round_budget_words;
inline constexpr hal::ProgramGeometryValues
    kDe10ProgramGeometry = hal::ProgramGeometryValues::measured(
        hal::board_by_id(hal::BoardId::De10Nano).program
    );
inline constexpr std::size_t kDe10ProgramChunkBytes = kDe10ProgramGeometry.chunk_bytes.bytes();

}
