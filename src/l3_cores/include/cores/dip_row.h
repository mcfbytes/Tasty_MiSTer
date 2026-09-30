// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mister::cores::mra {

inline constexpr std::size_t kMaxDipIds = 32;
inline constexpr std::size_t kDipTextChars = 31;

struct DipRow {
    std::string name;
    int start = 0;
    int size = 0;
    int num = 0;
    bool has_val = false;
    std::uint64_t mask = 0;
    std::vector<std::string> ids;
    std::array<std::uint64_t, kMaxDipIds> val{};
};

}  // namespace mister::cores::mra
