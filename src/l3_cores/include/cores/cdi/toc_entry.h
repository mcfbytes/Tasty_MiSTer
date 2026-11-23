// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cdi/q_control.h"
#include "svc/disc/toc_point.h"
#include "svc/msf.h"

namespace mister::cores::cdi {

using svc::disc::TocPoint;

struct TocEntry {
    QControl control;
    TocPoint point;
    svc::Msf at;
    friend constexpr bool operator==(TocEntry, TocEntry) = default;
};

}
