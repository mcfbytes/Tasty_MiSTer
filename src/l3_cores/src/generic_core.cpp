// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/generic_core.h"

#include <memory>

namespace mister::cores {

Ex<void> GenericCore::do_adopt(const CoreAdopt&) {

    return {};
}

Ex<void> GenericCore::on_mount(IoIndex slot, const MountedPath& p) {

    return stream_.mount(slot, p);
}

std::unique_ptr<Core> make_generic(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<GenericCore>(p, g.services);
}

}
