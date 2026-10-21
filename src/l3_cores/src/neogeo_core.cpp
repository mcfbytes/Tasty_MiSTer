// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo_core.h"

#include "cores/mounted_path.h"

namespace mister::cores {

Ex<void> NeoGeoCore::do_adopt(const CoreAdopt&) {
    return drive_.adopt();
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

Ex<proto::SessionParams> NeoGeoCore::stream_opening(IoIndex) {
    TASTY_SEAT_BODY(NeoGeoCore);
    tx_.opened();
    proto::SessionParams params{};
    params.ext = tx_.ext();
    return params;
}

void NeoGeoCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(NeoGeoCore);
    tx_.closed(end);
}

}
