// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/mailbox_servants.h"

#include "cores/msu_machine.h"

namespace mister::cores {

ServantSet make_servants(const svc::Vfs& vfs) {
    ServantSet set{};
    set[static_cast<std::size_t>(ServantId::Msu1)] = std::make_unique<MsuMachine>(vfs);
    return set;
}

}  // namespace mister::cores
