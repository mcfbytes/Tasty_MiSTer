// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/fixed_str.h"
#include "svc/types.h"

namespace mister::svc {

using DiscIdText = FixedStr<11, StrFit::Clip>;

struct DiscIdentity {
    DiscIdText id{};
    DiscRegion region = DiscRegion::Unknown;
};

}
