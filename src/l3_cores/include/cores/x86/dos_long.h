// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::x86 {

struct DosLong {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr DosLong decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint32_t))
            return {};
        return DosLong{infra::load_le<std::uint32_t>(bytes.first<sizeof(std::uint32_t)>())};
    }

    [[nodiscard]] static constexpr DosLong of(proto::FileOffset at) noexcept {
        return DosLong{static_cast<std::uint32_t>(at.seek_offset())};
    }

    [[nodiscard]] constexpr proto::FileOffset offset() const noexcept {
        return proto::FileOffset{v};
    }

    constexpr void store(std::span<std::byte, sizeof(std::uint32_t)> out) const noexcept {
        infra::store_le<std::uint32_t>(out, v);
    }

    friend constexpr bool operator==(DosLong, DosLong) = default;
};

}
