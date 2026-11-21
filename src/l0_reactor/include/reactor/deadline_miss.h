// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <type_traits>

#include "infra/prelude.h"
#include "reactor/service_slot.h"

namespace mister::reactor {

struct DeadlineMiss {
    std::chrono::duration<std::uint32_t, std::nano> over{};
    ServiceSlot slot{};
};

static_assert(std::is_trivially_copyable_v<DeadlineMiss>);

}
