// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/claim_set.h"
#include "hal/phys_region.h"

namespace mister::hal {

enum class WindowId : std::uint8_t {};
inline constexpr std::size_t kMaxWindows = 7;

using WindowClaims = infra::ClaimSet<WindowId, kMaxWindows>;
using WindowClaim = WindowClaims::Claim;

struct WindowDecl {
    WindowId id;
    PhysRegion region;
    const char* uio_name;
};

constexpr bool windows_well_formed(std::span<const WindowDecl> w) noexcept {
    if (w.size() > kMaxWindows) return false;
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (static_cast<std::size_t>(w[i].id) != i) return false;
    }
    return true;
}

namespace detail {
inline WindowId no_window_of_that_name() noexcept { return WindowId{}; }
}  // namespace detail

consteval WindowId window_named(std::span<const WindowDecl> w, std::string_view name) {
    const WindowDecl* hit = nullptr;
    for (const WindowDecl& d : w) {
        if (std::string_view(d.region.name) != name) continue;
        if (hit != nullptr) return detail::no_window_of_that_name();
        hit = &d;
    }
    return hit != nullptr ? hit->id : detail::no_window_of_that_name();
}

}  // namespace mister::hal
