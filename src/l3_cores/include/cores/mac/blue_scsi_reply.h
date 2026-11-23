// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mac/blue_scsi_wire.h"

namespace mister::cores::mac {

struct BlueScsiReply {
    BlueScsiStatus status = BlueScsiStatus::Check;
    infra::ByteCount bytes{};
};

}
