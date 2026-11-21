// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "svc/connect/bt_prompt.h"
#include "svc/connect/view_gen.h"

namespace mister::svc::connect {

struct BtPromptView {
    TASTY_SEAT_EXEMPT(component);

    ViewGen gen{};
    std::optional<BtPrompt> prompt{};
};

static_assert(std::is_trivially_copyable_v<BtPromptView>);

}
