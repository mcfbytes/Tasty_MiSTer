// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace mister::proto {

class SaveStateEngine;
class SavePrimer;

struct StateSlot {
    std::uint8_t v = 0;
    friend constexpr bool operator==(StateSlot, StateSlot) = default;

    [[nodiscard]] constexpr bool valid() const noexcept;
    [[nodiscard]] constexpr std::optional<StateSlot> next() const noexcept;

private:
    friend class SaveStateEngine;
    friend class SavePrimer;

    [[nodiscard]] constexpr std::uint32_t offset(std::uint32_t size) const noexcept {
        return std::uint32_t{v} * size;
    }

    [[nodiscard]] constexpr unsigned digit() const noexcept { return unsigned{v} + 1u; }
};

inline constexpr std::array<StateSlot, 4>
    kStateSlots{StateSlot{0}, StateSlot{1}, StateSlot{2}, StateSlot{3}};

constexpr bool StateSlot::valid() const noexcept {
    return v < kStateSlots.size();
}

constexpr std::optional<StateSlot> StateSlot::next() const noexcept {
    if (unsigned{v} + 1u >= kStateSlots.size())
        return std::nullopt;
    return StateSlot{static_cast<std::uint8_t>(v + 1u)};
}

}
