// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/agilex_5/agilex5_bridges.h"

namespace mister::boards::agilex_5 {

Ex<void> Bridges::pre_program() { return unimplemented(ERR_SITE()); }

Ex<void> Bridges::post_program() { return unimplemented(ERR_SITE()); }

Ex<void> Bridges::request_board_reset() { return unimplemented(ERR_SITE()); }

}  // namespace mister::boards::agilex_5
