// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/cause_reg.h"
#include "hal/doorbell_source.h"
#include "hal/phys_region.h"
#include "hal/register_window.h"
#include "hal/types.h"
#include "os/types.h"
#include "os/uio_handle.h"

namespace mister::hal {

class UioDoorbellSource final : public IDoorbellSource {
    TASTY_SEAT_RESIDENT(RT);

public:
    void set_lw_window(PhysRegion lw) noexcept { lw_ = lw; }

    void set_line_space(os::UioLineSpace s) noexcept { lines_ = s; }

    [[nodiscard]] Ex<os::UioHandle> open_line(os::UioLine line) override {
        TASTY_SEAT_BODY(UioDoorbellSource);
        return os::UioHandle::open(line, lines_);
    }
    [[nodiscard]] Ex<RegisterWindow<CauseReg>> map_cause(LwOffset base) override {
        TASTY_SEAT_BODY(UioDoorbellSource);
        return RegisterWindow<CauseReg>::map(lw_, base);
    }

private:
    PhysRegion lw_{};
    os::UioLineSpace lines_{};
};

}  // namespace mister::hal
