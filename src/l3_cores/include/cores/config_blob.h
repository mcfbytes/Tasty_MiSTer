// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

namespace mister::cores {

struct ConfigBlob {
    std::span<const std::byte> bytes{};
};

}  // namespace mister::cores
