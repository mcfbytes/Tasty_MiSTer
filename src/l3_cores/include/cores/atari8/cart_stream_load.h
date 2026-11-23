// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/atari8/atari8_machine.h"
#include "cores/stream_ask.h"
#include "cores/stream_load.h"
#include "cores/stream_load_end.h"
#include "cores/stream_load_host.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/aux_value.h"
#include "proto/session_params.h"

namespace mister::cores::atari8 {

class CartStreamLoad final : public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit CartStreamLoad(Atari8Machine machine) noexcept : machine_(machine) {}

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(const StreamAsk& ask) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] StreamLoadHost& host() noexcept { return host_; }

    [[nodiscard]] const StreamLoadHost& host() const noexcept { return host_; }

    static constexpr proto::AuxValue kConsoleCartStart{0x0200'4000};

private:
    StreamLoadHost host_;
    Atari8Machine machine_;
};

}
