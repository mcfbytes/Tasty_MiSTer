// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "hal/ocram_platform.h"
#include "hal/ocram_run.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "boards/cyclone_v/cyclone_v_bridges.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::boards::cyclone_v {

struct BridgeLends {
    hal::reg::Lend rstmgr;
    hal::reg::Lend sysmgr;
};

class OcramPlatform final : public hal::IOcramPlatform {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] static Ex<OcramPlatform> open(BridgeLends lends) noexcept;

    [[nodiscard]] bool f2h_bridge_released() const override;
    void disarm_warm_boot() override;
    void clear_ecc_flags() override;

private:
    OcramPlatform(hal::RegisterWindow<RstMgr> rst, hal::RegisterWindow<SysMgr> sys) noexcept
        : rst_(std::move(rst)),
          sys_(std::move(sys)) {}

    hal::RegisterWindow<RstMgr> rst_;
    hal::RegisterWindow<SysMgr> sys_;
};

}
