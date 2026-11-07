// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::svc::adv7513 {

enum class Presence : std::uint8_t {
    NotProbed = 0,
    Absent,
    Ambiguous,
    Rejected,
    Found,
};

enum class SubMapRole : std::uint8_t { Edid = 0, Spd, Cec };

inline constexpr std::size_t kSubMapCount = 3;

struct SubMapIndex {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr SubMapIndex of(SubMapRole role) noexcept {
        return SubMapIndex{static_cast<std::uint8_t>(role)};
    }

    friend constexpr bool operator==(SubMapIndex, SubMapIndex) = default;
};

struct ProbeReport {
    TASTY_SEAT_EXEMPT(component);
    Presence presence = Presence::NotProbed;

    std::optional<std::uint8_t> bus{};
    std::uint8_t acks = 0;
    std::uint8_t r41 = 0;
    std::uint8_t r42 = 0;

    infra::IndexedArray<bool, kSubMapCount, SubMapIndex> sub_acks{};
    std::uint32_t last_errno = 0;

    [[nodiscard]] bool ack(SubMapRole role) const noexcept {
        const auto p = sub_acks.find(SubMapIndex::of(role));
        return p.has_value() && *p;
    }

    void set_ack(SubMapRole role, bool on) noexcept {
        if (auto p = sub_acks.find(SubMapIndex::of(role)); p)
            *p = on;
    }

    [[nodiscard]] bool edid_ack() const noexcept { return ack(SubMapRole::Edid); }

    [[nodiscard]] bool spd_ack() const noexcept { return ack(SubMapRole::Spd); }

    [[nodiscard]] bool cec_ack() const noexcept { return ack(SubMapRole::Cec); }
};

}
