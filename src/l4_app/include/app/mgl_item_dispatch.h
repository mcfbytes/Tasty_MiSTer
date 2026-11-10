// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/mgl_item.h"
#include "infra/message_sum.h"

namespace mister::app {

using MglItemRoutes = infra::AllRouted;

[[nodiscard]] inline std::optional<MglItem::Load> load_of(const MglItem& m) noexcept {
    return infra::as<MglItem::Load>(m);
}

[[nodiscard]] inline std::optional<MglItem::Reset> reset_of(const MglItem& m) noexcept {
    return infra::as<MglItem::Reset>(m);
}

}
