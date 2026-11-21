// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::proto::link {
class Link;
}

namespace mister::svc::video {

struct ShadowMaskWord {
    std::uint32_t v = 0;

    [[nodiscard]] Ex<void> write_to(proto::link::Link& link) const;
    friend constexpr bool operator==(ShadowMaskWord, ShadowMaskWord) = default;
};

static_assert(sizeof(ShadowMaskWord) == 4);

}
