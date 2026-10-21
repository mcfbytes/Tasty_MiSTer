// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace mister::app {

[[nodiscard]] std::string ini_set_key(
    std::string_view text,
    std::string_view key,
    std::string_view value
);

}
