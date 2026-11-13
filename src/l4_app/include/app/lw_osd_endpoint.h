// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/lw_block_endpoint.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "hal/osd_block.h"
#include "hal/reg_port.h"
#include "hal/window_port.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/seat_owner_ui.h"
#include "proto/osd_surface.h"

namespace mister::app {

class LwOsdEndpoint : public LwBlockEndpoint<OsdGrant, OsdRoute, seat_owner::Ui> {
    TASTY_SEAT_RESIDENT(Ui);
    static_assert(
        seat_owner::Ui::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    using Port = hal::IRegPort<hal::OsdBlock>;

    struct Wiring {
        const OsdGrantCell& grant;
        OsdRouteCell& route;
        OsdLeaseCell& lease;
        const proto::OsdSurface& surface;
        infra::OptRef<Port> port{};
    };

    explicit LwOsdEndpoint(const Wiring& w) noexcept
        : LwBlockEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>(w.grant, w.lease),
          w_(w) {}

    [[nodiscard]] const OsdRoute& route() const noexcept { return tally_; }

private:
    static constexpr proto::OsdRowCount kRows = proto::OsdSurface::kPageRows;
    static_assert(
        kRows == proto::OsdRowCount{hal::OsdBlock::kImageRows}
            && proto::OsdSurface::kRowBytes == hal::OsdBlock::kRowBytes,
        "the shadow holds exactly the surface's page rows"
    );

    using Base = LwBlockEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;
    static_assert(
        std::is_same_v<OsdLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );
    void publish_hook_(const OsdRoute& r) noexcept override;
    void prepare_hook_(const OsdGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const OsdGrant& g) noexcept override;
    void bind_hook_(const OsdGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    void store_frame_() noexcept;
    [[nodiscard]] bool store_row_(proto::OsdRow row) noexcept;
    [[nodiscard]] std::uint32_t ctrl_() const noexcept;
    [[nodiscard]] proto::OsdRowCount bound_rows_() const noexcept;
    [[nodiscard]] Port& port_() noexcept;

    Wiring w_;
    std::optional<hal::WindowPort<hal::OsdBlock, seat_owner::Ui>> block_{};
    proto::OsdRowCells<std::uint32_t, kRows.size()> stored_{};
    std::uint32_t ctrl_stored_ = 0;
    proto::OsdSurface::Row row_{};
};

extern template class LwBlockEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;

}

namespace mister::hal {

extern template class WindowPort<OsdBlock, seat_owner::Ui>;
}

namespace mister::xthread {
extern template class GrantLease<app::OsdGrant, seat_owner::Ui>;
}
