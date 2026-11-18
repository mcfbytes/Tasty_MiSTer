// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "infra/seat.h"
#include "proto/core_session.h"
#include "proto/session_live.h"
#include "proto/status_cell.h"
#include "proto/status_change.h"
#include "proto/status_source.h"
#include "proto/status_word.h"

namespace mister::proto {

class StatusDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    StatusDecoder(
        CoreSession& session,
        StatusCell& cell,
        const ISessionLive& live,
        IStatusSource& source
    ) noexcept
        : session_(session),
          cell_(cell),
          live_(live),
          source_(source) {}

    void bind_source(IStatusSource& source) noexcept { source_ = source; }

    void service() noexcept;

    [[nodiscard]] bool active() const noexcept { return live_.session_live(); }

    [[nodiscard]] StatusChange last() const noexcept { return last_; }

    [[nodiscard]] std::uint32_t flush_refusals() const noexcept { return flush_refusals_; }

private:
    CoreSession& session_;
    StatusCell& cell_;
    const ISessionLive& live_;
    std::reference_wrapper<IStatusSource> source_;
    StatusChange last_{};
    std::uint32_t flush_refusals_ = 0;
    StatusWord words_{};
    StatusWord published_{};
};

}
