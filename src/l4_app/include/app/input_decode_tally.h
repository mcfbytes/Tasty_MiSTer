// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/counter.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::app {

class InputDecodeTally {
    TASTY_SEAT_RESIDENT(Input);

public:
    void round() noexcept { rounds_.add(1); }

    void events(std::uint32_t n) noexcept { events_.add(n); }

    void key_published() noexcept { keys_pub_.add(1); }

    void key_emitted() noexcept { keys_emit_.add(1); }

    void mouse_packet() noexcept { mouse_pkt_.add(1); }

    void kick() noexcept { kicks_.add(1); }

    void eviction() noexcept { evicted_.add(1); }

    void capture() noexcept { captures_.add(1); }

    [[nodiscard]] svc::JoyMask joy_seen() const noexcept { return joy_seen_; }

    [[nodiscard]] svc::PlayerMask joy_players() const noexcept { return joy_players_; }

    void merge_joy(svc::JoyMask seen, svc::PlayerMask players) noexcept {
        joy_seen_ = joy_seen_.with(seen);
        joy_players_ = joy_players_.with(players);
    }

    [[nodiscard]] std::uint32_t rounds() const noexcept { return rounds_.get(); }

    [[nodiscard]] std::uint32_t events() const noexcept { return events_.get(); }

    [[nodiscard]] std::uint32_t keys_published() const noexcept { return keys_pub_.get(); }

    [[nodiscard]] std::uint32_t keys_emitted() const noexcept { return keys_emit_.get(); }

    [[nodiscard]] std::uint32_t mouse_packets() const noexcept { return mouse_pkt_.get(); }

    [[nodiscard]] std::uint32_t kicks() const noexcept { return kicks_.get(); }

    [[nodiscard]] std::uint32_t evictions() const noexcept { return evicted_.get(); }

    [[nodiscard]] std::uint32_t captures() const noexcept { return captures_.get(); }

private:
    xthread::Counter rounds_{};
    xthread::Counter events_{};
    xthread::Counter keys_pub_{};
    xthread::Counter keys_emit_{};
    xthread::Counter mouse_pkt_{};
    xthread::Counter kicks_{};
    xthread::Counter evicted_{};

    svc::JoyMask joy_seen_{};
    svc::PlayerMask joy_players_{};
    xthread::Counter captures_{};
};

}
