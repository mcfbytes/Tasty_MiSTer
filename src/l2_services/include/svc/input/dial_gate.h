// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/paddle_pos.h"
#include "proto/spinner_delta.h"
#include "proto/spinner_travel.h"
#include "svc/input/dial_report.h"
#include "svc/input_gates.h"
#include "svc/types.h"

namespace mister::svc::input {

class DialGate {
    TASTY_SEAT_RESIDENT(Input);

public:
    struct Facts {
        std::optional<proto::PaddlePos> paddle{};
        std::optional<proto::SpinnerDelta> spin{};
        friend constexpr bool operator==(const Facts&, const Facts&) = default;
    };

    struct Counts {
        infra::Tally spin_drops{};
        infra::Tally spin_carries{};
        infra::Tally range_faults{};
    };

    [[nodiscard]] Facts admit(
        PlayerIndex player,
        const DialReport& decoded,
        InputGates gates,
        bool muted
    ) noexcept {
        const auto dials = players_.find(player);
        if (!dials)
            return {};
        if (decoded.paddle)
            dials->wanted = decoded.paddle;
        if (decoded.range_fault)
            counts_.range_faults.add();
        if (muted || gates.osd_visible) {
            drop_travel(*dials, decoded.travel);
            return {};
        }
        Facts out{};
        const proto::SpinnerTravel travel = dials->rest.plus(decoded.travel);
        if (!travel.none()) {
            out.spin = travel.delta();
            dials->rest = travel.rest();
            if (!dials->rest.none())
                counts_.spin_carries.add();
        }
        if (dials->wanted && dials->wanted != dials->carried)
            out.paddle = dials->wanted;
        return out;
    }

    void carried(PlayerIndex player, const Facts& sent) noexcept {
        const auto dials = players_.find(player);
        if (dials && sent.paddle)
            dials->carried = sent.paddle;
    }

    void forget_carried() noexcept {
        for (PlayerDials& dials : players_) {
            dials.carried.reset();
            drop_travel(dials, proto::SpinnerTravel{});
        }
    }

    [[nodiscard]] bool spin_owed() const noexcept {
        for (const PlayerDials& dials : players_)
            if (!dials.rest.none())
                return true;
        return false;
    }

    [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

private:
    struct PlayerDials {
        std::optional<proto::PaddlePos> wanted{};
        std::optional<proto::PaddlePos> carried{};
        proto::SpinnerTravel rest{};
    };

    void drop_travel(PlayerDials& dials, proto::SpinnerTravel decoded) noexcept {
        if (!dials.rest.none() || !decoded.none())
            counts_.spin_drops.add();
        dials.rest = proto::SpinnerTravel{};
    }

    infra::IndexedArray<PlayerDials, kMaxPlayers, PlayerIndex> players_{};
    Counts counts_{};
};

}
