// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/lw_block_endpoint.h"
#include "app/lw_line.h"
#include "app/sys_grant.h"
#include "app/sys_route.h"
#include "hal/read_window_port.h"
#include "hal/reg_port.h"
#include "hal/sys_block.h"
#include "infra/opt_ref.h"
#include "os/epoll_set.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "os/monotonic_clock.h"
#include "proto/status_snapshot.h"
#include "svc/io_coworker.h"

namespace mister::app {

class LwSysEndpoint final : public svc::IIoCoworker,
                            public LwBlockEndpoint<SysGrant, SysRoute, seat_owner::Io> {
    TASTY_SEAT_RESIDENT(Io);
    static_assert(
        seat_owner::Io::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    using Port = hal::IRegPort<hal::SysBlock, hal::RegAccess::ReadOnly>;

    static constexpr unsigned kMaxReads = 4;

    struct Wiring {
        const SysGrantCell& grant;
        SysRouteCell& route;
        SysLeaseCell& lease;
        proto::StatusSnapshotCell& snapshots;
        infra::OptRef<const os::EpollSet> doorbells{};
        infra::OptRef<Port> port{};
    };

    explicit LwSysEndpoint(const Wiring& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;
    void on_stop() noexcept override;

    [[nodiscard]] const SysRoute& route() const noexcept { return tally_; }

private:
    using Base = LwBlockEndpoint<SysGrant, SysRoute, seat_owner::Io>;
    static_assert(
        std::is_same_v<SysLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );
    void publish_hook_(const SysRoute& r) noexcept override;
    void prepare_hook_(const SysGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const SysGrant& g) noexcept override;
    void bind_hook_(const SysGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration gen) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    [[nodiscard]] Port& port_() noexcept;

    [[nodiscard]] std::optional<proto::StatusSnapshot> read_snapshot_() noexcept;
    void serve_status_(bool rang) noexcept;

    Wiring w_;
    std::optional<hal::ReadWindowPort<hal::SysBlock, seat_owner::Io>> block_{};
    LwLine line_{};
    bool recheck_ = false;
    std::optional<hal::SysBlock::Stflg> sent_stflg_{};
    os::MonotonicClock clock_{};
};

extern template class LwBlockEndpoint<SysGrant, SysRoute, seat_owner::Io>;

}

namespace mister::hal {

extern template class ReadWindowPort<SysBlock, seat_owner::Io>;
}

namespace mister::xthread {
extern template class GrantLease<app::SysGrant, seat_owner::Io>;
}
