// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/joy_level_emitter.h"

#include "app/link_tx_channel.h"

namespace mister::app {

void JoyLevelEmitter::publish(bool muted) {

    std::uint32_t seen = tally_.joy_seen();
    std::uint32_t players = tally_.joy_players();

    if (const std::uint32_t rs = wire_.edge_reset_seq(); rs != wm_edge_reset_) {
        wm_edge_reset_ = rs;
        for (std::uint32_t& v : last_joy_posted_)
            v = 0u;
    }
    const std::uint32_t gates = wire_.gates();
    wm_gates_ = gates;
    const bool grabbed = (gates & InputWire::kGateGrabbed) != 0u;
    const bool osd_visible = (gates & InputWire::kGateOsdVisible) != 0u;
    std::uint32_t autofire[svc::kMaxPlayers]{};

    for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
        const svc::PlayerIndex p{i};
        const svc::JoyMask mask = svc_.joy_mask(p);
        if (mask.v != 0u) {
            seen |= mask.v;
            players |= (1u << i);
        }
        autofire[i] = wire_.joy(p).autofire.v;

        wire_.publish_joy(p, mask, wire_.joy(p).autofire);
        if (mask.v != armed_mask_[i] || autofire[i] != armed_autofire_[i]) {
            armed_mask_[i] = mask.v;
            armed_autofire_[i] = autofire[i];
            zero_armed_[i] = true;
        }
        if (!muted && grabbed && mask.v != last_joy_posted_[i]) {

            if (inbox_.push(
                    proto::LinkOp::JoyEmit{
                        .player = proto::PlayerIndex{i},
                        .mask = mask,
                        .autofire = proto::JoyMask{autofire[i]}
                    }
                )) {
                last_joy_posted_[i] = mask.v;
                wire_.note_activity();
            }
        }
    }

    if (!grabbed || osd_visible) {
        for (std::uint8_t i = 0; i < svc::kMaxPlayers; ++i) {
            if (!zero_armed_[i])
                continue;
            const proto::LinkOp::JoyRelease rel{
                .player = proto::PlayerIndex{i},
                .mask = proto::JoyMask{grabbed ? (armed_mask_[i] | autofire[i]) : armed_mask_[i]}
            };
            if (!muted && inbox_.push(rel))
                wire_.note_activity();

            zero_armed_[i] = false;
        }
    }
    tally_.store_joy(seen, players);
}

}
