// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/prelude.h"
#include "svc/input/player_slots.h"
#include "svc/input/device_identity.h"

namespace mister::svc::input {

std::string_view PlayerSlots::key_of(const DeviceIdentity& id) noexcept {

    return id.merge_id.empty() ? std::string_view(id.id) : std::string_view(id.merge_id);
}

std::optional<PlayerIndex> PlayerSlots::slot_of(const DeviceIdentity& id) const {
    const std::string_view key = key_of(id);
    if (key.empty())
        return std::nullopt;
    for (const PlayerIndex player : kPlayers) {
        if (bound_[player].view() == key)
            return player;
    }
    return std::nullopt;
}

Ex<void> PlayerSlots::assign(const DeviceIdentity& id, PlayerIndex player) {
    const auto slot = bound_.find(player);
    if (!slot)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});

    if (!slot->assign(key_of(id)))
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});

    for (const PlayerIndex other : kPlayers) {
        if (other != player && bound_[other] == *slot)
            bound_[other].clear();
    }
    return {};
}

std::optional<PlayerIndex> PlayerSlots::first_free(PlayerMask live_mask) const {
    for (const PlayerIndex player : kPlayers) {
        if (!live_mask.contains(player))
            return player;
    }
    return std::nullopt;
}

}
