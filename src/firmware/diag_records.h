// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <tuple>

#include "bay_record.h"
#include "hd_record.h"
#include "rec_record.h"
#include "rg_record.h"
#include "rnd_record.h"
#include "round_trace_record.h"
#include "sess_record.h"
#include "sess_view.h"
#include "tas_record.h"

namespace mister::fw {

using DiagRecords = std::
    tuple<SessRecord, BayRecord, RndRecord, TasRecord, RecRecord, HdRecord, RgRecord>;

}
