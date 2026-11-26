// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/cyclone_v/cyclone_v_ocram_platform.h"

#include <utility>

#include "infra/byte_count.h"
#include "infra/prelude.h"

namespace mister::boards::cyclone_v {

Ex<OcramPlatform> OcramPlatform::open(BridgeLends lends) noexcept {
    if (!lends.rstmgr.lent()
        || lends.rstmgr.size() < RstMgr::kSize
        || !lends.sysmgr.lent()
        || lends.sysmgr.size() < SysMgr::kSize) {
        return std::unexpected(
            infra::ByteCount::of(lends.sysmgr.size()).refusal(Errc::dt_missing, ERR_SITE())
        );
    }
    return OcramPlatform(
        hal::RegisterWindow<RstMgr>::borrow(hal::reg::Block::redeem(lends.rstmgr)),
        hal::RegisterWindow<SysMgr>::borrow(hal::reg::Block::redeem(lends.sysmgr))
    );
}

bool OcramPlatform::f2h_bridge_released() const {
    TASTY_SEAT_BODY(OcramPlatform);
    return !rst_.read(RstMgr::Reg::BrgModReset).intersects(hal::RegWord{RstMgr::kF2hModule});
}

void OcramPlatform::disarm_warm_boot() {
    TASTY_SEAT_BODY(OcramPlatform);
    sys_.write(SysMgr::Reg::WarmRamGrpEnable, hal::RegWord{0u});
}

void OcramPlatform::clear_ecc_flags() {
    TASTY_SEAT_BODY(OcramPlatform);
    sys_.write(SysMgr::Reg::EccGrpOcram, hal::RegWord{SysMgr::kOcramEccFlags});
}

}
