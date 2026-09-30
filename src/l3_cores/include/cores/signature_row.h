// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mister::cores {

struct SignatureRow {
    std::uint32_t offset;
    std::span<const std::byte> magic;
    std::uint8_t region;
    std::string_view why;
};

}  // namespace mister::cores
