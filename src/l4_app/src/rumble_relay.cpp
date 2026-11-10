// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rumble_relay.h"

#include "proto/user_io/poll_shape.h"
#include "proto/user_io/rumble_motors.h"

namespace mister::app {

static_assert(
    proto::user_io::poll_row(proto::user_io::PollShape::RumblePoll).params == svc::kMaxPlayers,
    "the rumble row issues one parameter per player"
);

void RumbleRelay::refresh() noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    const svc::RumblePadSet now = out_.rumble_pads();
    const bool players_moved = now.players != seen_players_;
    for (const svc::PlayerIndex p : svc::kPlayers) {
        if (now.pads[p] == seen_[p].pad)
            continue;

        const std::uint16_t held = seen_[p].word;
        seen_[p].pad = now.pads[p];
        seen_[p].word = 0;
        if (held != 0 && now.players.has(p)) {
            ++reapplied_;
            apply_(p, held);
        }
    }
    if (!players_moved)
        return;
    seen_players_ = now.players;
    pads_.publish(now.players);
}

void RumbleRelay::on(const proto::LinkEvent::PolledWord& w) noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    const svc::PlayerIndex player{w.param};
    const auto held = seen_.find(player);
    if (!held) {
        ++out_of_range_;
        return;
    }
    if (w.word == held->word) {
        ++repeats_;
        return;
    }
    apply_(player, w.word);
}

void RumbleRelay::apply_(svc::PlayerIndex player, std::uint16_t word) noexcept {
    const proto::user_io::RumbleMotors m = proto::user_io::rumble_motors(word);
    switch (out_.rumble(player, m)) {
        case svc::RumbleApply::NoDevice:
            ++no_device_;
            return;
        case svc::RumbleApply::Applied:
            ++applied_;
            break;
        case svc::RumbleApply::Failed:

            if (failures_++ == 0)
                diag_.appendf(
                    "{\"t\":\"rumble_fail\",\"player\":%u,\"word\":%u}",
                    static_cast<unsigned>(player.v),
                    static_cast<unsigned>(word)
                );
            break;
    }
    seen_[player].word = word;
}

}
