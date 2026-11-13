// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/types.h"
#include "ui/types.h"

namespace mister::app {
class UiRequestRing;
}

namespace mister::ui {

class SdramLatch final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void on_mask(HdMask m, bool front_end, infra::OptRef<app::UiRequestRing> asks) noexcept;

    [[nodiscard]] std::optional<proto::OsdMask> cfg() const noexcept { return cfg_; }

    [[nodiscard]] svc::Tally refusals() const noexcept { return refusals_; }

private:
    std::optional<proto::OsdMask> cfg_{};
    svc::Tally refusals_{};
    bool reported_ = false;
};

}
