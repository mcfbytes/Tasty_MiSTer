// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/input/rumble_relay.h"

#include "proto/user_io/poll_shape.h"
#include "proto/user_io/rumble_motors.h"

namespace mister::app::input {

static_assert(
    proto::user_io::poll_row(proto::user_io::PollShape::RumblePoll).params == svc::kMaxPlayers,
    "the rumble row issues one parameter per player"
);

void RumbleRelay::refresh() noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    const svc::input::RumblePadSet now = out_.rumble_pads();
    const bool players_moved = now.players != seen_players_;
    for (const svc::PlayerIndex p : svc::kPlayers) {
        if (now.pads[p] == seen_[p].pad)
            continue;

        const hal::LinkWord held = seen_[p].word;
        seen_[p].pad = now.pads[p];
        seen_[p].word = hal::LinkWord{};
        if (held != hal::LinkWord{} && now.players.contains(p)) {
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
    on_level(svc::PlayerIndex{w.param}, w.word);
}

void RumbleRelay::on_level(svc::PlayerIndex player, hal::LinkWord word) noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    const auto held = seen_.find(player);
    if (!held) {
        ++out_of_range_;
        return;
    }
    if (word == held->word) {
        ++repeats_;
        return;
    }
    apply_(player, word);
}

void RumbleRelay::apply_(svc::PlayerIndex player, hal::LinkWord word) noexcept {
    const proto::user_io::RumbleMotors m = proto::user_io::rumble_motors(word);
    switch (out_.rumble(player, m)) {
        case svc::input::RumbleApply::NoDevice:
            ++no_device_;
            return;
        case svc::input::RumbleApply::Applied:
            ++applied_;
            break;
        case svc::input::RumbleApply::Failed:

            if (failures_++ == 0)
                diag_.appendf(
                    "{\"t\":\"rumble_fail\",\"player\":%s,\"word\":%s}",
                    player.text().c_str(),
                    word.text().c_str()
                );
            break;
    }
    seen_[player].word = word;
}

}
