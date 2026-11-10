// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/json_out.h"

namespace mister::infra {

struct Heartbeat {
    std::uint64_t v = 0;

    friend constexpr void json_field(JsonOut& o, JsonName key, Heartbeat h) noexcept {
        o.field(key, h.v);
    }

    friend constexpr bool operator==(Heartbeat, Heartbeat) = default;
};

}
