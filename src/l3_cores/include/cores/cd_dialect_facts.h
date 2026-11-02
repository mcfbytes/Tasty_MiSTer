// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_profile.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

struct CdDialectFacts {
    const CdProfile& profile;
    svc::DiscReadService& disc;
};

}
