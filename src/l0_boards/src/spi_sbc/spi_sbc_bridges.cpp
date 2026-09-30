// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/spi_sbc/spi_sbc_bridges.h"

namespace mister::boards {

Ex<void> SpiSbcBridges::pre_program() { return unimplemented(ERR_SITE()); }

Ex<void> SpiSbcBridges::post_program() { return unimplemented(ERR_SITE()); }

Ex<void> SpiSbcBridges::request_board_reset() { return unimplemented(ERR_SITE()); }

}  // namespace mister::boards
