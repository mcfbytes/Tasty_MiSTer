// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "app/grant_endpoint.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "hal/ocram.h"
#include "hal/osd_block.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_owner_ui.h"
#include "proto/osd_surface.h"

namespace mister::app {

class OcramOsdEndpoint : public GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui> {
    TASTY_SEAT_RESIDENT(Ui);
    static_assert(
        seat_owner::Ui::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    struct Wiring {
        const OsdGrantCell& grant;
        OsdRouteCell& route;
        OsdLeaseCell& lease;
        const proto::OsdSurface& surface;
    };

    explicit OcramOsdEndpoint(const Wiring& w) noexcept
        : GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>(w.grant, w.lease),
          w_(w) {}

    [[nodiscard]] const OsdRoute& route() const noexcept { return tally_; }

private:
    static constexpr proto::OsdRowCount kRows{hal::OsdBlock::kImageRows};
    static_assert(
        proto::OsdSurface::kMaxRows <= kRows,
        "the surface never composes past the image"
    );
    static_assert(
        proto::OsdSurface::kRowBytes == hal::OsdBlock::kRowBytes,
        "the image holds exactly the surface's row bytes"
    );

    using Base = GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;
    static_assert(
        std::is_same_v<OsdLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );

    [[nodiscard]] bool accepts_hook_(const OsdGrant& g) const noexcept override;
    void publish_hook_(const OsdRoute& r) noexcept override;
    void prepare_hook_(const OsdGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const OsdGrant& g) noexcept override;
    void bind_hook_(const OsdGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    [[nodiscard]] bool publish_row_(proto::OsdRow r) noexcept;
    [[nodiscard]] proto::OsdRowCount bound_rows_() const noexcept;

    Wiring w_;
    std::optional<hal::ocram::RingWriter<seat_owner::Ui>> writer_{};
    proto::OsdRowCount image_rows_{kRows};

    proto::OsdRowCells<infra::CellGen, kRows.size()> stored_{};
    proto::OsdSurface::Row row_{};
};

extern template class GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;

}

namespace mister::infra::xthread {
extern template class GrantLease<app::OsdGrant, seat_owner::Ui>;
}
