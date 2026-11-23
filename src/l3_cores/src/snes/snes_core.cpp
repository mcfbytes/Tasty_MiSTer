// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes/snes_core.h"

#include "cores/core_adopt.h"
#include "cores/snes/snes_msu_decl.h"
#include "infra/prelude.h"

namespace mister::cores::snes {

SnesCore::SnesCore(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      mailbox_(MailboxCarriage::granted(h, kSnesMailbox)) {}

Ex<void> SnesCore::do_adopt(const CoreAdopt&) {

    return {};
}

Ex<void> SnesCore::on_mount(IoIndex, const MountedPath&) {

    return unimplemented(ERR_SITE());
}

}
