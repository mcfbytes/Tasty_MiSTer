// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd/cd_profile.h"
#include "svc/disc/disc_read_service.h"

namespace mister::cores::cd {

struct CdDialectFacts {
    const CdProfile& profile;
    svc::disc::DiscReadService& disc;
};

}
