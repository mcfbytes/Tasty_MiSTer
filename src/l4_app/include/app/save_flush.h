// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::cores {
class Core;
}

namespace mister::app {

class HeldTransfer;
class NvramUpload;

class SaveFlush {
    TASTY_SEAT_RESIDENT(RT);

public:
    SaveFlush(NvramUpload& upload, HeldTransfer& held, infra::OptRef<cores::Core> core) noexcept
        : upload_(upload),
          held_(held),
          core_(core) {}

    [[nodiscard]] bool counted() const noexcept;

private:
    NvramUpload& upload_;
    HeldTransfer& held_;
    infra::OptRef<cores::Core> core_;
};

}
