// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::cores {

struct CompanionAsk {
    std::string_view path{};
    bool mailbox_live = false;
    std::uint64_t aperture_base = 0;
};

}  // namespace mister::cores
