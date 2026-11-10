// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/nvram_store.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

[[nodiscard]] SavePath pick_disc_path(
    const svc::Vfs& vfs,
    const SavePath& own,
    const SavePath& stock,
    proto::FileSize length
);

}
