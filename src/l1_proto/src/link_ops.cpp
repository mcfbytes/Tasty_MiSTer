// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link_ops.h"

namespace mister::proto {

Ex<SaveRequestWord> LinkOps::read_save_request() {
    auto r = link_.command(UioOp::ChkUpload);
    if (!r)
        return std::unexpected(r.error());
    link_.close();
    return SaveRequestWord{*r};
}

}
