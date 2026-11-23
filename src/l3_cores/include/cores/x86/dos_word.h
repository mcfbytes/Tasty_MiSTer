// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_count.h"
#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosWord {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr DosWord decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint16_t))
            return {};
        return DosWord{infra::load_le<std::uint16_t>(bytes.first<sizeof(std::uint16_t)>())};
    }

    [[nodiscard]] static constexpr DosWord of(infra::ByteCount bytes) noexcept {
        return DosWord{static_cast<std::uint16_t>(bytes.v)};
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    constexpr void store(std::span<std::byte, 2> out) const noexcept {
        infra::store_le<std::uint16_t>(out, v);
    }

    friend constexpr bool operator==(DosWord, DosWord) = default;
};

}
