// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd_engine.h"

namespace mister::cores {

struct CdCommandWords {
    std::size_t count = 0;
    std::array<std::uint16_t, CdEngine::kMaxCommandWords> words{};

    [[nodiscard]] std::span<const std::uint16_t> view() const noexcept {
        return {words.data(), count};
    }
};

}
