// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mac/mac_core.h"

namespace mister::cores::mac {

MacCore::MacCore(const CoreProfile& p, const CoreGrant& g)
    : Core(p, g.services),
      geometry_(p.lba_windows),
      servants_(g.services.slot_servants) {}

Ex<void> MacCore::do_adopt(const CoreAdopt&) {
    return {};
}

Ex<void> MacCore::on_mount(IoIndex slot, const MountedPath& p) {
    return stream_.mount(slot, p);
}

}
