// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/core_session.h"
#include "proto/session_live.h"
#include "proto/status_cell.h"
#include "proto/status_register.h"
#include "proto/status_word.h"

namespace mister::proto {

class SpiStatusDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    SpiStatusDecoder(hal::ISpiTransport& link, CoreSession& session, StatusCell& cell,
                     const ISessionLive& live) noexcept
        : link_(&link), session_(&session), cell_(&cell), live_(&live) {}

    void service() noexcept;
    [[nodiscard]] bool active() const noexcept { return live_->session_live(); }

    [[nodiscard]] std::uint8_t last() const noexcept { return last_; }

    [[nodiscard]] std::uint32_t flush_refusals() const noexcept { return flush_refusals_; }

private:
    hal::ISpiTransport* link_;
    CoreSession* session_;
    StatusCell* cell_;
    const ISessionLive* live_;
    std::uint8_t last_ = 0;
    std::uint32_t flush_refusals_ = 0;
    std::array<std::uint16_t, StatusRegister::kWords> words_{};
    StatusWord published_{};
};

}  // namespace mister::proto
