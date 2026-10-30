// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "hal/spi_transport.h"
#include "infra/seat.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/session_live.h"
#include "proto/types.h"
#include "proto/user_io/poll_params.h"
#include "proto/user_io/poll_shape.h"
#include "reactor/link_decoder_decl.h"

namespace mister::proto::user_io {

using reactor::LinkDecoderDecl;
inline constexpr auto kHostPollRows = std::to_array<LinkDecoderDecl>({
    {"link.info_poll", reactor::Cause::Tick, nullptr, 100, reactor::RoundClaim::Never},
    {"link.rumble_poll", reactor::Cause::Tick, nullptr, 0, reactor::RoundClaim::Never},
    {"link.f12_mod_poll", reactor::Cause::None, nullptr, 0, reactor::RoundClaim::Never},
});
static_assert(kHostPollRows.size() == kPollShapes.size(), "one census row per poll shape");

consteval bool start_rows_have_no_cause() {
    for (std::size_t i = 0; i < kPollShapes.size(); ++i) {
        const bool at_start = kPollShapes[i].cadence == Cadence::SessionStart;
        if (at_start != (kHostPollRows[i].cause == reactor::Cause::None))
            return false;
    }
    return true;
}

static_assert(start_rows_have_no_cause(), "a session-start row is never dispatched by the census");

class PollReader {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::PolledWord>;

    PollReader(
        hal::ISpiTransport& link,
        ILinkRouter& router,
        const ISessionLive& live,
        IPollParams& params,
        PollShape shape
    ) noexcept
        : link_(link),
          live_(live),
          params_(params),
          shape_(shape),
          out_(router) {}

    void service() noexcept;

    void start_beat() noexcept;

    [[nodiscard]] bool active() const noexcept { return armed_ && live_.session_live(); }

    void arm(bool declared, BindGeneration gen) noexcept;

    [[nodiscard]] reactor::LinkDecoderDecl census_row(
        const reactor::ILinkDecoder& impl
    ) const noexcept;

    [[nodiscard]] bool armed() const noexcept { return armed_; }

    [[nodiscard]] std::uint32_t polls() const noexcept { return polls_; }

    [[nodiscard]] std::uint32_t pushes() const noexcept { return pushes_; }

    [[nodiscard]] std::uint32_t declined() const noexcept { return declined_; }

    [[nodiscard]] std::uint32_t errors() const noexcept { return errors_; }

    [[nodiscard]] std::uint16_t mirror(std::uint8_t param) const noexcept {
        return param < mirror_.size() ? mirror_[param] : std::uint16_t{0};
    }

private:
    void issue_(const PollShapeRow& row) noexcept;
    void beat_(const PollShapeRow& row, std::uint8_t param) noexcept;
    [[nodiscard]] bool push_(PollShape shape, std::uint8_t param, std::uint16_t word) noexcept;

    hal::ISpiTransport& link_;
    const ISessionLive& live_;
    IPollParams& params_;
    PollShape shape_;
    Port out_;
    bool armed_ = false;
    BindGeneration gen_{};
    std::array<std::uint16_t, kMaxPollParams> mirror_{};
    std::uint32_t polls_ = 0;
    std::uint32_t pushes_ = 0;
    std::uint32_t declined_ = 0;
    std::uint32_t errors_ = 0;
};

}
