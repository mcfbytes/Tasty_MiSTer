// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_rows.h"

#include "app/input_wire.h"
#include "app/link_binder.h"
#include "app/osd_wire.h"
#include "app/session_bindings.h"
#include "cores/core_support.h"
#include "cores/registry.h"
#include "proto/core_session.h"

namespace mister::app {

SessionRows::SessionRows(const Wiring& w) noexcept
    : session_(w.session),
      bindings_(w.bindings),
      binder_(w.binder),
      osd_(w.osd),
      input_(w.input),
      identify_decoder_(w.signals, session_, w.router, bindings_.generation()),
      status_decoder_(w.link, session_, status_cell_, w.live),
      axi_status_decoder_(session_, status_cell_, w.live),
      status_lw_(w.status_lw),
      conf_str_decoder_(w.link, session_, w.router, bindings_.generation()),
      block_decoder_(w.link, w.router, w.slots, w.live),
      info_poll_(
          w.link,
          w.router,
          w.live,
          static_cast<proto::user_io::IPollParams&>(*this),
          proto::user_io::PollShape::InfoPoll
      ),
      rumble_poll_(
          w.link,
          w.router,
          w.live,
          static_cast<proto::user_io::IPollParams&>(*this),
          proto::user_io::PollShape::RumblePoll
      ) {}

std::string_view SessionRows::conf_str_name() const noexcept {
    return bindings_.core_name();
}

[[nodiscard]] Ex<void> SessionRows::rebind_census(
    std::span<const reactor::LinkDecoderDecl> rows,
    cores::Core* owner
) {
    TASTY_SEAT_BODY(SessionRows);
    return binder_.bind_census(rows, owner);
}

[[nodiscard]] Ex<void> SessionRows::rebind_pre_session(
    proto::BindGeneration gen,
    cores::Core* owner
) {
    TASTY_SEAT_BODY(SessionRows);
    bindings_.bind_generation(gen);
    const std::array<reactor::LinkDecoderDecl, 2> rows{
        {{"link.identify",
          reactor::Cause::Tick,
          &identify_row_,
          0,
          reactor::DeadlineClass::A,
          reactor::OsdBudget::Shared},
         {"link.conf_str",
          reactor::Cause::Tick,
          &conf_str_row_,
          0,
          reactor::DeadlineClass::A,
          reactor::OsdBudget::Shared}}
    };
    return rebind_census(rows, owner);
}

std::uint8_t SessionRows::issued_params(proto::user_io::PollShape s) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    switch (s) {
        case proto::user_io::PollShape::InfoPoll:
        case proto::user_io::PollShape::F12ModPoll:
            return 0x01;
        case proto::user_io::PollShape::RumblePoll:

            if (const auto m = rumble_pads_reader_.take_if_changed(input_.rumble_pads_cell()))
                rumble_pads_ = *m;
            return rumble_pads_;
        case proto::user_io::PollShape::kCount:
            break;
    }
    return 0;
}

std::size_t SessionRows::put_census_row_(
    std::span<reactor::LinkDecoderDecl> out,
    std::size_t n,
    const reactor::LinkDecoderDecl& row
) noexcept {
    if (n < out.size()) {
        out[n] = row;
        return n + 1;
    }
    ++tally_.census_row_drops;
    return n;
}

std::span<const reactor::LinkDecoderDecl> SessionRows::census_rows(
    std::array<reactor::LinkDecoderDecl, kMaxServices>& out,
    infra::OptRef<cores::Core> current
) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    std::size_t n = 0;

    if (current.has_value() && session_.type() == proto::CoreType::EightBit) {
        const cores::CoreProfile& p = current->profile();
        if (!p.is_front_end
            && p.block_service == cores::BlockService::Generic
            && block_poll_ == BlockPollRoute::Link) {
            n = put_census_row_(
                out,
                n,
                reactor::LinkDecoderDecl{
                    "link.block",
                    reactor::Cause::Tick,
                    &block_row_,
                    0,
                    reactor::DeadlineClass::A,
                    reactor::OsdBudget::Claimed
                }
            );
        }
    }
    if (current.has_value()) {
        for (const reactor::LinkDecoderDecl& r : current->services())
            n = put_census_row_(out, n, r);
    }

    if (current.has_value()
        && session_.type() == proto::CoreType::EightBit
        && !cores::profile_for(conf_str_name()).is_front_end) {

        n = put_census_row_(
            out,
            n,
            status_route_ == StatusRoute::Lw
                ? reactor::
                      LinkDecoderDecl{"link.status_lw", reactor::Cause::Tick, &axi_status_row_, 0, reactor::DeadlineClass::A, reactor::OsdBudget::Shared}
                : reactor::LinkDecoderDecl{
                      "link.status_poll",
                      reactor::Cause::Tick,
                      &status_row_,
                      0,
                      reactor::DeadlineClass::A,
                      reactor::OsdBudget::Shared
                  }
        );

        if (bindings_.declares_info())
            n = put_census_row_(out, n, info_poll_.census_row(info_row_));

        n = put_census_row_(out, n, rumble_poll_.census_row(rumble_row_));
    }
    return std::span<const reactor::LinkDecoderDecl>(out.data(), n);
}

void SessionRows::bind_status_route(StatusRoute route, std::uint16_t gen) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    status_route_ = status_lw_ ? route : StatusRoute::Link;
    axi_status_decoder_.bind(
        status_route_ == StatusRoute::Lw ? status_lw_
                                         : infra::OptRef<const proto::StatusSnapshotCell>{},
        gen
    );
}

std::optional<bool> SessionRows::latch_osd_edge() {
    TASTY_SEAT_BODY(SessionRows);
    const bool vis = osd_.has_focus();
    if (vis != osd_visible_last_) {
        osd_visible_last_ = vis;
        return vis;
    }
    return std::nullopt;
}

void SessionRows::publish_status_word() noexcept {
    TASTY_SEAT_BODY(SessionRows);
    status_scratch_ = session_.status().value();
    status_cell_.publish(status_scratch_);
}

void SessionRows::disarm_polls() noexcept {
    TASTY_SEAT_BODY(SessionRows);
    info_poll_.arm(false, bindings_.generation());
    rumble_poll_.arm(false, bindings_.generation());
}

void SessionRows::arm_session_polls(bool front_end) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    info_poll_.arm(!front_end && bindings_.declares_info(), bindings_.generation());
    rumble_poll_.arm(!front_end, bindings_.generation());
}

void SessionRows::bind_block_drain(infra::OptRef<cores::Core> current) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    block_decoder_.bind_budget(current->profile().block_drain_budget);
    block_decoder_.bind_sector_floor(current->profile().block_sector_floor);
}

}
