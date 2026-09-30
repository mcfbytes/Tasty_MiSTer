// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "infra/seat.h"

namespace mister::cores {

class ICoreWindow;

inline constexpr std::size_t kMaxCoreWindows = 4;

struct CoreWindowGrant {
    TASTY_SEAT_EXEMPT(component);
    std::array<std::string_view, kMaxCoreWindows> names{};
    std::array<ICoreWindow*, kMaxCoreWindows> windows{};

    [[nodiscard]] ICoreWindow* named(std::string_view name) const noexcept {
        for (std::size_t i = 0; i < kMaxCoreWindows; ++i) {
            if (!names[i].empty() && names[i] == name) return windows[i];
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (ICoreWindow* w : windows)
            n += w != nullptr ? 1u : 0u;
        return n;
    }

    [[nodiscard]] static CoreWindowGrant single(std::string_view name, ICoreWindow* w) noexcept {
        CoreWindowGrant g;
        g.names[0] = name;
        g.windows[0] = w;
        return g;
    }
};

}  // namespace mister::cores
