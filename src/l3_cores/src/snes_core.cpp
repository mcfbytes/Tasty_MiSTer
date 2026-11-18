// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_core.h"

#include "cores/core_adopt.h"
#include "cores/manifests/snes.h"

namespace mister::cores {

SnesCore::SnesCore(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      mailbox_(MailboxCarriage::granted(h, manifests::kSnesMailboxFrame)) {}

Ex<void> SnesCore::do_adopt(const CoreAdopt&) {

    return {};
}

Ex<void> SnesCore::on_mount(IoIndex, const MountedPath&) {

    return unimplemented(ERR_SITE());
}

}
