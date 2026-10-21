// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_core.h"

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores {

CdCore::CdCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host)
    : Core(profile, host),
      drive_(
          *this,
          cd,
          host,
          profile.windows.empty() ? nullptr : host.windows.named(profile.windows[0].region.name)
      ) {}

Ex<void> CdCore::do_adopt(const CoreAdopt&) {
    return drive_.adopt();
}

Ex<void> CdCore::on_mount(IoIndex, const MountedPath& p) {
    return drive_.arm_disc(p);
}

Ex<proto::SessionParams> CdCore::stream_opening(IoIndex) {
    TASTY_SEAT_BODY(CdCore);
    if (auto r = drive_.eject_for_cart(); !r)
        return std::unexpected(r.error());
    tx_.opened();
    proto::SessionParams params{};
    params.ext = tx_.ext();
    return params;
}

void CdCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(CdCore);
    tx_.closed(end);
}

}
