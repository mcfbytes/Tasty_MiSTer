// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::svc {

struct DeviceIdentity;

class PlayerSlots {
    TASTY_SEAT_EXEMPT(component);

public:
    PlayerSlots() = default;

    std::optional<PlayerIndex> slot_of(const DeviceIdentity& id) const;
    [[nodiscard]] Ex<void> assign(const DeviceIdentity& id, PlayerIndex player);

    std::optional<PlayerIndex> first_free(PlayerMask live_mask) const;

    static std::string_view key_of(const DeviceIdentity& id) noexcept;

    std::string_view key_at(PlayerIndex player) const noexcept {
        const auto key = bound_.find(player);
        return key ? key->view() : std::string_view{};
    }

private:
    static constexpr std::size_t kKeyReserve = 128;

    using StickyKey = FixedStr<kKeyReserve, StrFit::Reject>;
    infra::IndexedArray<StickyKey, kMaxPlayers, PlayerIndex> bound_{};
};

}
