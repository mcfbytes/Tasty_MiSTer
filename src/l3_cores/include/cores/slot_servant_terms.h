// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mount_reads.h"
#include "cores/remount_asked.h"
#include "infra/opt_ref.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct SlotServantTerms {
    const svc::Vfs& vfs;
    infra::OptRef<RemountCell> remount{};
    infra::OptRef<MountReadsCell> mount_reads{};
};

}
