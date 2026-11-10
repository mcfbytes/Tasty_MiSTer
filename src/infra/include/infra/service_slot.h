// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/json_out.h"

namespace mister {

inline constexpr std::size_t kMaxServices = 24;

struct ServiceSlot {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<ServiceSlot> of(std::size_t i) noexcept {
        if (i > kMaxServices)
            return std::nullopt;
        return ServiceSlot{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr ServiceSlot next() const noexcept {
        return ServiceSlot{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr std::size_t index() const noexcept { return v; }

    [[nodiscard]] constexpr std::uint8_t wire() const noexcept { return v; }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        ServiceSlot slot
    ) noexcept {
        o.field(key, slot.v);
    }

    friend constexpr void json_elem(infra::JsonOut& o, ServiceSlot slot) noexcept {
        o.elem(slot.v);
    }

    friend constexpr bool operator==(ServiceSlot, ServiceSlot) = default;
    friend constexpr auto operator<=>(ServiceSlot, ServiceSlot) = default;
};

}
