// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "hal/window_decl.h"

namespace mister::boards::cyclone_v {

enum class Window : std::uint8_t { FpgaMgr, FpgaMgrData, Sdr, RstMgr, SysMgr, Nic301, LwBridge };
inline constexpr std::size_t kWindowCount = std::to_underlying(Window::LwBridge) + 1u;
static_assert(kWindowCount <= hal::kMaxWindows);

[[nodiscard]] constexpr hal::WindowId id(Window w) noexcept {
    return hal::WindowId{std::to_underlying(w)};
}

}  // namespace mister::boards::cyclone_v
