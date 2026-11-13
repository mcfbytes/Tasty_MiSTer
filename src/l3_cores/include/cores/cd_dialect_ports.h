// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_link.h"
#include "infra/seat.h"
#include "svc/disc_read_service.h"

namespace mister::hal {
class DiskLamp;
}

namespace mister::cores {

struct CdDialectPorts {
    TASTY_SEAT_RESIDENT(RT);

    const svc::DiscGeometry& geom;
    ICdLink& link;
    infra::OptRef<hal::DiskLamp> lamp{};

    [[nodiscard]] bool egress_busy() const noexcept {
        return link.egress_bound() && !link.egress_idle();
    }
};

}
