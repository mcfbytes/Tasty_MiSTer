// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/path_text.h"
#include "infra/fixed_str.h"

namespace mister::app {

struct Mgl final {
    enum class Slot : std::uint8_t { File, Image };

    FixedStr<kPathMax, StrFit::Clip> rbf{};
    FixedStr<kPathMax, StrFit::Clip> path{};
    Slot slot = Slot::File;
    std::uint8_t index = 0;
    std::uint16_t delay_s = 0;

    [[nodiscard]] bool complete() const noexcept {
        return !rbf.view().empty() && !path.view().empty();
    }
};

}
