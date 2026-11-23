// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/park_timeout.h"

namespace mister::svc {

struct NicWindowRow {

    enum class Memory : std::uint8_t { StronglyOrdered, WriteCombined };

    Memory memory = Memory::StronglyOrdered;

    infra::ParkTimeout poll{};

    bool measured = false;
};

inline constexpr NicWindowRow kNicWindow{
    .memory = NicWindowRow::Memory::WriteCombined,
    .poll = infra::ParkTimeout{1},
    .measured = false,
};

}
