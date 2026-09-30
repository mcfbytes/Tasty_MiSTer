// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::fw {

enum class SupervisorVerdict : std::uint8_t { Continue, Exit };

[[nodiscard]] constexpr SupervisorVerdict supervisor_verdict(bool stop_latched,
                                                             bool rt_exited) noexcept {
    return (stop_latched || rt_exited) ? SupervisorVerdict::Exit : SupervisorVerdict::Continue;
}

}  // namespace mister::fw
