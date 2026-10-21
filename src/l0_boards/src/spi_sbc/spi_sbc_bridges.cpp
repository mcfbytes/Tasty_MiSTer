// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/spi_sbc/spi_sbc_bridges.h"

namespace mister::boards::spi_sbc {

Ex<void> Bridges::pre_program() {
    return unimplemented(ERR_SITE());
}

Ex<void> Bridges::post_program() {
    return unimplemented(ERR_SITE());
}

Ex<void> Bridges::request_board_reset() {
    return unimplemented(ERR_SITE());
}

}
