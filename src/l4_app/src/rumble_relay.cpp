// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rumble_relay.h"

#include "proto/user_io/poll_shape.h"
#include "proto/user_io/rumble_poll.h"

namespace mister::app {

static_assert(
    proto::user_io::poll_row(proto::user_io::PollShape::RumblePoll).params == svc::kMaxPlayers,
    "the rumble row issues one parameter per player"
);

void RumbleRelay::refresh() noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    const svc::RumblePadSet now = out_.rumble_pads();
    if (now == pads_seen_)
        return;
    const bool players_moved = now.players != pads_seen_.players;
    for (std::uint8_t p = 0; p < last_.size(); ++p) {
        if (now.pads[p] == pads_seen_.pads[p])
            continue;

        const std::uint16_t held = last_[p];
        last_[p] = 0;
        if (held != 0 && (now.players & (1u << p)) != 0u) {
            ++reapplied_;
            apply_(p, held);
        }
    }
    pads_seen_ = now;
    if (players_moved)
        pads_.publish(now.players);
}

void RumbleRelay::on(const proto::LinkEvent::PolledWord& w) noexcept {
    TASTY_SEAT_BODY(RumbleRelay);
    if (w.param >= last_.size()) {
        ++out_of_range_;
        return;
    }
    if (w.word == last_[w.param]) {
        ++repeats_;
        return;
    }
    apply_(w.param, w.word);
}

void RumbleRelay::apply_(std::uint8_t player, std::uint16_t word) noexcept {
    const proto::user_io::RumbleMotors m = proto::user_io::rumble_motors(word);
    switch (out_.rumble(svc::PlayerIndex{player}, m.strong, m.weak)) {
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
                    static_cast<unsigned>(player),
                    static_cast<unsigned>(word)
                );
            break;
    }
    last_[player] = word;
}

}
