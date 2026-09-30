// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/spi_transport.h"
#include "proto/core_session.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::proto {

class SpiConfStrDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::ConfStr>;

    SpiConfStrDecoder(hal::ISpiTransport& link, CoreSession& session, ILinkRouter& router,
                      const BindGeneration& gen) noexcept
        : link_(&link), session_(&session), gen_(&gen), out_(router) {}

    void service() noexcept;
    [[nodiscard]] bool active() const noexcept { return session_->conf_str_window_open(); }
    [[nodiscard]] std::uint32_t reads() const noexcept { return reads_; }

    [[nodiscard]] std::uint32_t intern_refusals() const noexcept { return intern_refusals_; }

    [[nodiscard]] std::uint32_t lost() const noexcept { return lost_; }

private:
    [[nodiscard]] Ex<std::size_t> read_() noexcept;
    void publish_(std::size_t n) noexcept;
    void refuse_(Errc code) noexcept;
    [[nodiscard]] bool push_body_(std::size_t n) noexcept;

    hal::ISpiTransport* link_;
    CoreSession* session_;
    const BindGeneration* gen_;
    Port out_;

    std::array<std::uint8_t, CoreSession::kConfStrCap - 1> buf_{};
    std::uint32_t reads_ = 0;
    std::uint32_t intern_refusals_ = 0;
    std::uint32_t lost_ = 0;
};

}  // namespace mister::proto
