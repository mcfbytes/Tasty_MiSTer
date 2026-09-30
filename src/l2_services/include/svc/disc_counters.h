// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc {

struct DiscCounters {
    std::uint32_t sync_decompress = 0;
    std::uint32_t park_timeouts = 0;
    std::uint32_t prefetch_refusals = 0;
};

static_assert(std::is_trivially_copyable_v<DiscCounters>);
static_assert(std::is_nothrow_default_constructible_v<DiscCounters>);

}  // namespace mister::svc
