// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cores/types.h"

namespace mister::cores::mra {

inline constexpr std::size_t kMaxDipIds = 32;
inline constexpr std::size_t kDipTextChars = 31;

struct DipChoiceRow {
    std::string id;
    DipWord val{};
};

struct DipField {
    int start = 0;
    int width = 0;

    [[nodiscard]] constexpr DipWord mask() const noexcept {
        return DipWord::ones(width).shifted(start);
    }

    [[nodiscard]] constexpr DipWord place(DipWord val) const noexcept { return val.shifted(start); }

    friend constexpr bool operator==(DipField, DipField) = default;
};

struct DipRow {
    std::string name;
    DipField field{};
    bool has_val = false;
    std::vector<DipChoiceRow> choices;
};

}
