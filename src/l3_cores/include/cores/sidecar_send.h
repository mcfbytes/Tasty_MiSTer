// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "cores/sidecar_plan.h"
#include "infra/seat.h"
#include "infra/tally.h"

namespace mister::cores {

class SidecarSend {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Waiting, Done };

    explicit SidecarSend(SidecarPlan&& plan);

    void append(SidecarPlan&& more);

    [[nodiscard]] Pass step(ILadderHost& host);

    [[nodiscard]] infra::Tally sent() const noexcept { return sent_; }

    [[nodiscard]] infra::Tally failures() const noexcept { return failures_; }

private:
    std::deque<SidecarPayload> owed_{};
    std::optional<PayloadPieces> pieces_{};
    infra::Tally sent_{};
    infra::Tally failures_{};
};

}
