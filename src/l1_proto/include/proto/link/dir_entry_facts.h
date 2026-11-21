// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"
#include "proto/dir_entry_kind.h"
#include "proto/link/dir_version.h"

namespace mister::proto::link {

struct DirEntryFacts {
    DirEntryKind kind;
    DirMajor major;
    hal::LwExtent block;
};

}
