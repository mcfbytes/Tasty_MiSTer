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

namespace mister::proto::link {

using reactor::LinkDecoderDecl;
inline constexpr auto kHostPollRows = std::to_array<LinkDecoderDecl>({
    {"link.info_poll",
     reactor::Cause::Tick,
     nullptr,
     std::chrono::milliseconds{100},
     reactor::RoundClaim::Never},
    {"link.rumble_poll",
     reactor::Cause::Tick,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
    {"link.f12_mod_poll",
     reactor::Cause::None,
     nullptr,
     reactor::kEveryRound,
     reactor::RoundClaim::Never},
});
static_assert(kHostPollRows.size() == user_io::kPollShapes.size(), "one census row per poll shape");

consteval bool start_rows_have_no_cause() {
    for (std::size_t i = 0; i < user_io::kPollShapes.size(); ++i) {
        const bool at_start = user_io::kPollShapes[i].cadence == user_io::Cadence::SessionStart;
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
        user_io::IPollParams& params,
        user_io::PollShape shape
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

    [[nodiscard]] hal::SpiWord mirror(std::uint8_t param) const noexcept {
        return param < mirror_.size() ? mirror_[param] : hal::SpiWord{};
    }

private:
    void issue_(const user_io::PollShapeRow& row) noexcept;
    void beat_(const user_io::PollShapeRow& row, std::uint8_t param) noexcept;
    [[nodiscard]] bool push_(
        user_io::PollShape shape,
        std::uint8_t param,
        hal::SpiWord word
    ) noexcept;

    hal::ISpiTransport& link_;
    const ISessionLive& live_;
    user_io::IPollParams& params_;
    user_io::PollShape shape_;
    Port out_;
    bool armed_ = false;
    BindGeneration gen_{};
    std::array<hal::SpiWord, kMaxPollParams> mirror_{};
    std::uint32_t polls_ = 0;
    std::uint32_t pushes_ = 0;
    std::uint32_t declined_ = 0;
    std::uint32_t errors_ = 0;
};

}
