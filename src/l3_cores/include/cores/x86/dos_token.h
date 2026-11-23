// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosToken {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr DosToken decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint16_t))
            return {};
        return DosToken{infra::load_le<std::uint16_t>(bytes.first<2>())};
    }

    friend constexpr bool operator==(DosToken, DosToken) = default;
};

}
