// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/player_slots.h"
#include "svc/device_identity.h"

namespace mister::svc {

std::string_view PlayerSlots::key_of(const DeviceIdentity& id) noexcept {

    return id.merge_id.empty() ? std::string_view(id.id) : std::string_view(id.merge_id);
}

std::optional<PlayerIndex> PlayerSlots::slot_of(const DeviceIdentity& id) const {
    const std::string_view key = key_of(id);
    if (key.empty())
        return std::nullopt;
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (!bound_[i].empty() && bound_[i] == key)
            return PlayerIndex{i};
    }
    return std::nullopt;
}

Ex<void> PlayerSlots::assign(const DeviceIdentity& id, PlayerIndex player) {
    if (player.v >= kMaxPlayers) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), player.v});
    }

    const std::string_view key = key_of(id);
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (i != player.v && !bound_[i].empty() && bound_[i] == key)
            bound_[i].clear();
    }
    bound_[player.v] = key;
    return {};
}

std::optional<PlayerIndex> PlayerSlots::first_free(std::uint32_t live_mask) const {
    for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
        if ((live_mask & (1u << i)) == 0u)
            return PlayerIndex{i};
    }
    return std::nullopt;
}

}
