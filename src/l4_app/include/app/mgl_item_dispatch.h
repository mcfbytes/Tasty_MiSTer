// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "app/mgl.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept MglItemSink = requires(S& s, const MglItem& m) {
    s.on(std::declval<const MglItem::Load&>());
    s.on(std::declval<const MglItem::Reset&>());
    s.misrouted(m);
};

using MglItemRoutes = infra::AllRouted;

[[nodiscard]] inline std::optional<MglItem::Load> load_of(const MglItem& m) noexcept {
    return infra::as<MglItem::Load>(m);
}

[[nodiscard]] inline std::optional<MglItem::Reset> reset_of(const MglItem& m) noexcept {
    return infra::as<MglItem::Reset>(m);
}

[[nodiscard]] inline DelaySeconds delay_of(const MglItem& m) noexcept {
    if (const auto ld = load_of(m); ld)
        return ld->delay_s;
    if (const auto rs = reset_of(m); rs)
        return rs->delay_s;
    return DelaySeconds{};
}

[[nodiscard]] inline std::optional<MglItem::Slot> slot_of(const MglItem& m) noexcept {
    const auto ld = load_of(m);
    if (!ld)
        return std::nullopt;
    return ld->slot;
}

[[nodiscard]] inline bool is_file_load(const MglItem& m) noexcept {
    const auto s = slot_of(m);
    return s && *s == MglItem::Slot::File;
}

}
