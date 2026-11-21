// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"

namespace mister::cores::cd {

struct SectorIo {
    IoIndex data;
    IoIndex cdda;
    IoIndex subcode;
};

}
