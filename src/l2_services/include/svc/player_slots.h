// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr unsigned kMaxPlayers = 6;

struct DeviceIdentity;

class PlayerSlots {
    TASTY_SEAT_EXEMPT(component);

public:
    std::optional<PlayerIndex> slot_of(const DeviceIdentity& id) const;
    Ex<void> assign(const DeviceIdentity& id, PlayerIndex player);

    std::optional<PlayerIndex> first_free(std::uint32_t live_mask) const;

    static std::string_view key_of(const DeviceIdentity& id) noexcept;

    std::string_view key_at(std::uint8_t player) const noexcept {
        return player < kMaxPlayers ? std::string_view(bound_[player]) : std::string_view{};
    }

    PlayerSlots() {
        for (std::string& b : bound_)
            b.reserve(kKeyReserve);
    }

private:
    static constexpr std::size_t kKeyReserve = 128;
    std::string bound_[kMaxPlayers]{};
};

}  // namespace mister::svc
