// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/q_control.h"
#include "cores/toc_point.h"
#include "svc/msf.h"

namespace mister::cores::cdi {

struct TocEntry {
    QControl control;
    TocPoint point;
    svc::Msf at;
    friend constexpr bool operator==(TocEntry, TocEntry) = default;
};

}
