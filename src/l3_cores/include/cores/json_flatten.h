// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "infra/error.h"

namespace mister::cores {

struct JsonCap {
    std::size_t v = 0;

    [[nodiscard]] constexpr bool exceeded_by(std::string_view text) const noexcept {
        return text.size() > v;
    }
};

[[nodiscard]] Ex<std::string> flatten_json(
    std::string_view json,
    std::string_view prefix,
    JsonCap cap
);

}
