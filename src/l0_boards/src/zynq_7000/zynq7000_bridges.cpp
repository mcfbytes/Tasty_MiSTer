// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/zynq_7000/zynq7000_bridges.h"

namespace mister::boards::zynq_7000 {

Ex<void> Bridges::pre_program() { return unimplemented(ERR_SITE()); }

Ex<void> Bridges::post_program() { return unimplemented(ERR_SITE()); }

Ex<void> Bridges::request_board_reset() { return unimplemented(ERR_SITE()); }

}  // namespace mister::boards::zynq_7000
