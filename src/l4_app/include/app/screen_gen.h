// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/serial.h"

namespace mister::app {

struct ScreenGen {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;

    [[nodiscard]] constexpr ScreenGen next() const noexcept { return infra::serial::next(*this); }

    friend constexpr bool operator==(ScreenGen, ScreenGen) = default;
};

static_assert(
    sizeof(ScreenGen) == 4 && std::is_trivially_copyable_v<ScreenGen>,
    "the published state and its ack stay one word"
);

}
