// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_file_access.h"
#include "svc/file_table_backing.h"
#include "svc/subcode_sidecar.h"
#include "svc/toc.h"
#include "svc/types.h"

namespace mister::svc {

struct CueMount {
    const DiscFileAccess& io;
    Toc& toc;
    FileTableBacking& table;
    SubcodeSidecar& subcode;
    CddaOrder& cdda_order;
};

}
