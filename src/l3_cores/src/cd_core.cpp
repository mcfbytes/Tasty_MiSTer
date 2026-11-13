// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_core.h"

#include "cores/core_adopt.h"
#include "cores/mount_answer.h"
#include "cores/mounted_path.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {

CdCore::CdCore(const CoreProfile& profile, const CdProfile& cd, const HostServices& host)
    : Core(profile, host),
      drive_(
          cd,
          host,
          profile.windows.empty() ? infra::OptRef<ICoreWindow>{}
                                  : host.windows.named(profile.windows[0].region.name)
      ) {}

Ex<void> CdCore::do_adopt(const CoreAdopt&) {
    return drive_.adopt();
}

MountState CdCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(CdCore);
    if (const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
        !m) {
        return mount_answer_of(m.error());
    }
    return drive_.disc_mount_state();
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
