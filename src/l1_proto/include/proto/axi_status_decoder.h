// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/core_session.h"
#include "proto/session_live.h"
#include "proto/status_cell.h"
#include "proto/status_register.h"
#include "proto/status_snapshot.h"
#include "proto/status_word.h"

namespace mister::proto {

class AxiStatusDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    AxiStatusDecoder(CoreSession& session, StatusCell& cell, const ISessionLive& live) noexcept
        : session_(&session),
          cell_(&cell),
          live_(&live) {}

    void bind(infra::OptRef<const StatusSnapshotCell> src, std::uint16_t gen) noexcept;

    void service() noexcept;

    [[nodiscard]] bool active() const noexcept { return live_->session_live(); }

    [[nodiscard]] std::uint8_t last() const noexcept { return last_; }

    [[nodiscard]] std::uint32_t flush_refusals() const noexcept { return flush_refusals_; }

    [[nodiscard]] std::uint32_t adopted() const noexcept { return adopted_; }

    [[nodiscard]] std::uint32_t stale() const noexcept { return stale_; }

private:
    CoreSession* session_;
    StatusCell* cell_;
    const ISessionLive* live_;
    infra::OptRef<const StatusSnapshotCell> src_{};
    StatusSnapshotCell::Reader reader_{};
    std::uint16_t gen_ = 0;
    std::uint8_t last_ = 0;
    std::uint32_t flush_refusals_ = 0;
    std::uint32_t adopted_ = 0;
    std::uint32_t stale_ = 0;
    StatusSnapshot scratch_{};
    std::array<std::uint16_t, StatusRegister::kWords> words_{};
    StatusWord published_{};
};

}
