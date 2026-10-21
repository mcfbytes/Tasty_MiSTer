// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "infra/error.h"

namespace mister::svc {

struct InflateResult {
    bool ok = false;
    std::vector<std::uint8_t> data;
};

[[nodiscard]] Ex<InflateResult> inflate_gzip(std::span<const std::uint8_t> src, std::size_t cap);

}
