// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/endian.h"

namespace mister::svc {

struct NicHostSeed {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr NicHostSeed of_name(std::string_view host) noexcept {
        constexpr std::uint32_t kFnvBasis = 2166136261u;
        constexpr std::uint32_t kFnvPrime = 16777619u;
        std::uint32_t h = kFnvBasis;
        for (const char c : host)
            h = (h ^ static_cast<unsigned char>(c)) * kFnvPrime;
        return NicHostSeed{h};
    }

    [[nodiscard]] constexpr NicHostSeed folded(NicHostSeed other) const noexcept {
        return NicHostSeed{v ^ other.v};
    }

    constexpr void store_be(std::span<std::byte, sizeof(std::uint32_t)> out) const noexcept {
        infra::store_be<std::uint32_t>(out, v);
    }

    friend constexpr bool operator==(NicHostSeed, NicHostSeed) = default;
};

}
