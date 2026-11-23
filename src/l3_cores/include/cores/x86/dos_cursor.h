// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosCursor {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr DosCursor decode(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() < sizeof(std::uint16_t))
            return {};
        return DosCursor{infra::load_le<std::uint16_t>(bytes.first<sizeof(std::uint16_t)>())};
    }

    [[nodiscard]] constexpr DosCursor next() const noexcept {
        return DosCursor{static_cast<std::uint16_t>(v + 1u)};
    }

    template <class T>
    [[nodiscard]] constexpr const T* in(std::span<const T> items) const noexcept {
        const std::size_t at{v};
        return at < items.size() ? &items[at] : nullptr;
    }

    constexpr void store(std::span<std::byte, 2> out) const noexcept {
        infra::store_le<std::uint16_t>(out, v);
    }

    friend constexpr bool operator==(DosCursor, DosCursor) = default;
};

}
