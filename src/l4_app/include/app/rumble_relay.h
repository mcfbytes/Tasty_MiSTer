// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/rumble_players_cell.h"
#include "infra/diag_log.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/types.h"
#include "svc/rumble_out.h"

namespace mister::app {

class RumbleRelay {
    TASTY_SEAT_RESIDENT(Input);

public:
    RumbleRelay(svc::IRumbleOut& out, RumblePlayersCell& pads, xthread::DiagLog& diag) noexcept
        : out_(out),
          pads_(pads),
          diag_(diag) {}

    RumbleRelay(const RumbleRelay&) = delete;
    RumbleRelay& operator=(const RumbleRelay&) = delete;

    void refresh() noexcept;

    void on(const proto::LinkEvent::PolledWord& w) noexcept;

    [[nodiscard]] std::uint16_t last_applied(proto::PlayerIndex player) const noexcept {
        if (const infra::OptRef<const Seen> held = seen_.find(player))
            return held->word;
        return std::uint16_t{0};
    }

    [[nodiscard]] std::uint32_t applied() const noexcept { return applied_; }

    [[nodiscard]] std::uint32_t repeats() const noexcept { return repeats_; }

    [[nodiscard]] std::uint32_t no_device() const noexcept { return no_device_; }

    [[nodiscard]] std::uint32_t failures() const noexcept { return failures_; }

    [[nodiscard]] std::uint32_t out_of_range() const noexcept { return out_of_range_; }

    [[nodiscard]] std::uint32_t reapplied() const noexcept { return reapplied_; }

private:
    void apply_(svc::PlayerIndex player, std::uint16_t word) noexcept;

    svc::IRumbleOut& out_;
    RumblePlayersCell& pads_;
    xthread::DiagLog& diag_;

    struct Seen {
        svc::PadSignature pad{};
        std::uint16_t word = 0;
    };

    infra::IndexedArray<Seen, svc::kMaxPlayers, svc::PlayerIndex> seen_{};
    svc::PlayerMask seen_players_{};
    std::uint32_t applied_ = 0;
    std::uint32_t repeats_ = 0;
    std::uint32_t no_device_ = 0;
    std::uint32_t failures_ = 0;
    std::uint32_t out_of_range_ = 0;
    std::uint32_t reapplied_ = 0;
};

}
