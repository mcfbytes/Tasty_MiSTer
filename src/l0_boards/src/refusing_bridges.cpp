// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/refusing_bridges.h"

#include "infra/prelude.h"

namespace mister::boards {

Ex<void> RefusingBridges::pre_program() {
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingBridges::post_program() {
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingBridges::request_board_reset() {
    return unimplemented(ERR_SITE());
}

}
