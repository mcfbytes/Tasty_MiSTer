// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

class ICoreWindow;

inline constexpr std::size_t kMaxCoreWindows = 4;

struct CoreWindowGrant {
    TASTY_SEAT_EXEMPT(component);

    struct Row {
        std::string_view name{};
        infra::OptRef<ICoreWindow> window{};
    };

    std::array<Row, kMaxCoreWindows> rows{};

    [[nodiscard]] ICoreWindow* named(std::string_view name) const noexcept {
        for (const Row& row : rows) {
            if (row.name.empty() || row.name != name)
                continue;
            if (!row.window)
                return nullptr;
            return &*row.window;
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (const Row& row : rows)
            n += row.window ? 1u : 0u;
        return n;
    }

    [[nodiscard]] static CoreWindowGrant single(std::string_view name, ICoreWindow* w) noexcept {
        CoreWindowGrant g;
        g.rows[0].name = name;
        if (w != nullptr)
            g.rows[0].window = infra::OptRef<ICoreWindow>{*w};
        return g;
    }
};

}
