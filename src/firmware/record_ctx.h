// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <optional>

#include "app/diag_counters.h"
#include "infra/json_out.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::fw {

struct RecordCtx {
    const app::DiagCounters& dc;
};

template <class R>
concept DiagRecord = requires(const R& r, const RecordCtx& c) {
    { R::kKey } -> std::convertible_to<infra::JsonName>;
    typename R::View;
    { r.sample(c) } -> std::same_as<std::optional<typename R::View>>;
};

template <class T, SeatTag S>
[[nodiscard]] std::optional<T> sample_published(const xthread::Telemetry<T, S>& cell) noexcept {
    T v{};
    if (cell.sample_into(v) == 0)
        return std::nullopt;
    return v;
}

template <class T, SeatTag S>
[[nodiscard]] std::optional<T> sample_published(
    infra::OptRef<const xthread::Telemetry<T, S>> cell
) noexcept {
    if (!cell)
        return std::nullopt;
    return sample_published(*cell);
}

}
