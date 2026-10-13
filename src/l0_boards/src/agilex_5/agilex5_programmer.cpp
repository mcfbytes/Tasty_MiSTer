// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/agilex_5/agilex5_programmer.h"

namespace mister::boards::agilex_5 {

Ex<bool> Programmer::program_begin() { return unimplemented(ERR_SITE()); }

Ex<void> Programmer::program_chunk(std::span<const std::byte> chunk) {
    (void)chunk;
    return unimplemented(ERR_SITE());
}

Ex<bool> Programmer::program_end() { return unimplemented(ERR_SITE()); }
Ex<bool> Programmer::program_step() { return unimplemented(ERR_SITE()); }

bool Programmer::programmed() const { return false; }

}  // namespace mister::boards::agilex_5
