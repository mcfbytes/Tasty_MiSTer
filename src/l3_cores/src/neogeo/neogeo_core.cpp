// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo/neogeo_core.h"
#include "cores/core_adopt.h"
#include "cores/cd/mount_answer.h"

#include "cores/mounted_path.h"
#include "infra/prelude.h"

namespace mister::cores::neogeo {

Ex<void> NeoGeoCore::do_adopt(const CoreAdopt&) {
    return drive_.adopt();
}

MountState NeoGeoCore::mount_disc(std::string_view path) {
    TASTY_SEAT_BODY(NeoGeoCore);
    if (const auto m = mount(profile().staging.disc_slot, MountedPath{path, proto::FileSize{}});
        !m) {
        return cd::mount_answer_of(m.error());
    }
    return drive_.disc_mount_state();
}

Ex<void> NeoGeoCore::do_reset() {
    if (!cd_mode_)
        return {};
    return drive_.reset();
}

Ex<void> NeoGeoCore::on_mount(IoIndex, const MountedPath& p) {
    cd_mode_ = true;
    return drive_.arm_disc(p);
}

}
