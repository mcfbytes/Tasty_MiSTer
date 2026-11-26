// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_rows.h"

#include "app/input/rumble_players_cell.h"
#include "app/link_binder.h"
#include "app/osd_wire.h"
#include "app/session_bindings.h"
#include "cores/core.h"
#include "infra/prelude.h"
#include "proto/link/core_session.h"

namespace mister::app {

SessionRows::SessionRows(const Wiring& w) noexcept
    : session_(w.session),
      bindings_(w.bindings),
      binder_(w.binder),
      osd_(w.osd),
      rumble_players_(w.rumble_players),
      identify_decoder_(w.signals, session_, w.router, bindings_.generation()),
      status_decoder_(session_, status_cell_, w.live, spi_status_source_),
      spi_status_source_(w.link),
      status_snapshots_(w.status_snapshots),
      conf_str_decoder_(w.link, session_, w.router, bindings_.generation()),
      block_decoder_(w.router, w.slots, w.live),
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
      ),
      declared_polls_{
          DeclaredPoll{w, poll_params(), kDeclarablePolls[0]},
          DeclaredPoll{w, poll_params(), kDeclarablePolls[1]}
      } {}

[[nodiscard]] Ex<void> SessionRows::rebind_census(
    std::span<const reactor::LinkDecoderDecl> rows,
    infra::OptRef<cores::Core> owner
) {
    TASTY_SEAT_BODY(SessionRows);
    return binder_.bind_census(rows, owner);
}

[[nodiscard]] Ex<void> SessionRows::rebind_pre_session(
    proto::BindGeneration gen,
    infra::OptRef<cores::Core> owner
) {
    TASTY_SEAT_BODY(SessionRows);
    bindings_.bind_generation(gen);
    const std::array<reactor::LinkDecoderDecl, 2> rows{
        {{"link.identify",
          reactor::Cause::Tick,
          &identify_row_,
          reactor::kEveryRound,
          reactor::RoundClaim::Never},
         {"link.conf_str",
          reactor::Cause::Tick,
          &conf_str_row_,
          reactor::kEveryRound,
          reactor::RoundClaim::Never}}
    };
    return rebind_census(rows, owner);
}

proto::ParamSet SessionRows::issued_params(proto::user_io::PollShape s) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    if (s != proto::user_io::PollShape::RumblePoll)
        return proto::ParamSet{};

    if (const auto m = rumble_pads_reader_.take_if_changed(rumble_players_))
        rumble_pads_ = *m;
    return rumble_pads_.as_param_set();
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

    if (current.has_value() && session_.terms().polls_block) {
        const cores::CoreProfile& p = current->profile();
        if (!p.host.front_end && p.block_service == cores::BlockService::Generic) {
            n = put_census_row_(
                out,
                n,
                reactor::LinkDecoderDecl{
                    "link.block",
                    reactor::Cause::Tick,
                    &block_row_,
                    reactor::kEveryRound,
                    reactor::RoundClaim::OnWork
                }
            );
        }
    }
    if (current.has_value()) {
        for (const reactor::LinkDecoderDecl& r : current->services())
            n = put_census_row_(out, n, r);

        for (const proto::user_io::PollShape shape : current->profile().polls) {
            for (const DeclaredPoll& poll : declared_polls_)
                if (poll.reader.shape() == shape)
                    n = put_census_row_(out, n, poll.reader.census_row(poll.row));
        }
    }

    if (current.has_value()
        && session_.terms().polls_status
        && !bindings_.record().identity.policy().front_end) {

        n = put_census_row_(
            out,
            n,
            !link_carries(status_route_)
                ? reactor::
                      LinkDecoderDecl{"link.status_lw", reactor::Cause::Tick, &status_row_, reactor::kEveryRound, reactor::RoundClaim::Never}
                : reactor::LinkDecoderDecl{
                      "link.status_poll",
                      reactor::Cause::Tick,
                      &status_row_,
                      reactor::kEveryRound,
                      reactor::RoundClaim::Never
                  }
        );

        const bool polls = link_carries(events_route_);
        if (polls && bindings_.declares_info())
            n = put_census_row_(out, n, info_poll_.census_row(info_row_));

        if (polls)
            n = put_census_row_(out, n, rumble_poll_.census_row(rumble_row_));
    }
    return std::span<const reactor::LinkDecoderDecl>(out.data(), n);
}

void SessionRows::bind_status_route(Carrier route, proto::BuildGeneration gen) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    status_route_ = status_snapshots_ ? route : Carrier::Spi;
    if (!link_carries(status_route_)) {
        snapshot_status_source_.bind(status_snapshots_, gen);
        status_decoder_.bind_source(snapshot_status_source_);
    } else {
        snapshot_status_source_.bind(infra::OptRef<const proto::link::StatusSnapshotCell>{}, gen);
        status_decoder_.bind_source(spi_status_source_);
    }
}

void SessionRows::bind_events_route(Carrier route) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    events_route_ = route;
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

void SessionRows::on_fabric_reprogrammed() noexcept {
    TASTY_SEAT_BODY(SessionRows);
    status_decoder_.on_fabric_reprogrammed();
}

void SessionRows::disarm_polls() noexcept {
    TASTY_SEAT_BODY(SessionRows);
    info_poll_.arm(false, bindings_.generation());
    rumble_poll_.arm(false, bindings_.generation());
    for (DeclaredPoll& poll : declared_polls_)
        poll.reader.arm(false, bindings_.generation());
}

void SessionRows::arm_session_polls(
    bool front_end,
    std::span<const proto::user_io::PollShape> declared
) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    info_poll_.arm(!front_end && bindings_.declares_info(), bindings_.generation());
    rumble_poll_.arm(!front_end, bindings_.generation());
    for (DeclaredPoll& poll : declared_polls_) {
        bool declares = false;
        for (const proto::user_io::PollShape shape : declared)
            declares = declares || shape == poll.reader.shape();
        poll.reader.arm(declares, bindings_.generation());
    }
}

void SessionRows::bind_block_drain(infra::OptRef<cores::Core> current) noexcept {
    TASTY_SEAT_BODY(SessionRows);
    block_decoder_.bind_budget(current->profile().block_drain_budget);
    block_decoder_.bind_sector_floor(current->profile().block_sector_floor);
}

}
