// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_flush.h"

#include "app/held_transfer.h"
#include "app/nvram_upload.h"

namespace mister::app {

bool SaveFlush::counted() const noexcept {
    TASTY_SEAT_BODY(SaveFlush);
    const bool fenced = core_ && held_.save_pull_fenced(core_);
    return upload_.save_upload_counted(NvramUpload::SaveCtx{.core = core_, .fenced = fenced});
}

}
