// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

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
    constexpr std::uint64_t kBytesPerMiB = 1u << 20;
    return rate.floor
        + std::chrono::nanoseconds{static_cast<std::int64_t>(
            (bytes.file_word<std::uint64_t>() * static_cast<std::uint64_t>(rate.per_mib.count()))
            / kBytesPerMiB
        )};
}

}
