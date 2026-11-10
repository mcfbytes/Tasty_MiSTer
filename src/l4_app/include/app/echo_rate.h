// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "proto/types.h"

namespace mister::app {

struct EchoRate {
    std::chrono::nanoseconds floor{};
    std::chrono::nanoseconds per_mib{};
};

[[nodiscard]] constexpr std::chrono::nanoseconds echo_bound(
    proto::FileSize bytes,
    EchoRate rate
) noexcept {
    return rate.floor + bytes.mib_share_of(rate.per_mib);
}

}
