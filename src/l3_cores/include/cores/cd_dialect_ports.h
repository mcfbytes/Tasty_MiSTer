// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_profile.h"
#include "cores/cd_sector_egress.h"
#include "cores/cd_transport.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

struct CdDialectPorts {
    TASTY_SEAT_RESIDENT(RT);

    const CdProfile& profile;
    svc::DiscReadService& disc;
    const svc::DiscGeometry& geom;
    ICdTransport& tx;
    infra::OptRef<ICdSectorEgress> egress{};

    [[nodiscard]] bool egress_busy() const noexcept {
        return egress.has_value() && !egress->egress_idle();
    }
};

}
