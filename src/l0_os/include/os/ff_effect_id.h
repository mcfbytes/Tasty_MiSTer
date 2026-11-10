// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <linux/input.h>

#include <cstdint>

namespace mister::os {

class FfEffectId {
public:
    constexpr FfEffectId() noexcept = default;

    [[nodiscard]] static constexpr FfEffectId from_kernel(std::int16_t id) noexcept {
        return FfEffectId{id};
    }

    [[nodiscard]] constexpr std::int16_t to_kernel() const noexcept { return v; }

    constexpr void stamp_into(ff_effect& effect) const noexcept { effect.id = v; }

    [[nodiscard]] constexpr input_event play_event() const noexcept {
        input_event e{};
        e.type = EV_FF;
        e.code = static_cast<std::uint16_t>(to_kernel());
        e.value = 1;
        return e;
    }

    friend constexpr bool operator==(FfEffectId, FfEffectId) = default;

private:
    friend class Evdev;

    [[nodiscard]] int erase_on(int fd) const noexcept;

    explicit constexpr FfEffectId(std::int16_t id) noexcept : v(id) {}

    std::int16_t v = -1;
};

static_assert(sizeof(FfEffectId) == 2);

}
