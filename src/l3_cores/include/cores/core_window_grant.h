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

    struct Row {
        std::string_view name{};
        infra::OptRef<ICoreWindow> window{};
    };

    std::array<Row, kMaxCoreWindows> rows{};

    [[nodiscard]] infra::OptRef<ICoreWindow> named(std::string_view name) const noexcept {
        for (const Row& row : rows) {
            if (row.name.empty() || row.name != name)
                continue;
            return row.window;
        }
        return {};
    }

    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t n = 0;
        for (const Row& row : rows)
            n += row.window ? 1u : 0u;
        return n;
    }

    [[nodiscard]] static CoreWindowGrant single(
        std::string_view name,
        infra::OptRef<ICoreWindow> w
    ) noexcept {
        CoreWindowGrant g;
        g.rows[0].name = name;
        g.rows[0].window = w;
        return g;
    }
};

}
