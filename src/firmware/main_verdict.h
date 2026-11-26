// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::fw {

enum class MainVerdict : std::uint8_t { Continue, Exit };

[[nodiscard]] constexpr MainVerdict main_verdict(bool stop_latched, bool rt_exited) noexcept {
    return (stop_latched || rt_exited) ? MainVerdict::Exit : MainVerdict::Continue;
}

}
