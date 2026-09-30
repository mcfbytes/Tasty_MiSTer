// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/zynq_7000/zynq7000_programmer.h"

namespace mister::boards {

Ex<bool> Zynq7000Programmer::program_begin() { return unimplemented(ERR_SITE()); }

Ex<void> Zynq7000Programmer::program_chunk(std::span<const std::byte> chunk) {
    (void)chunk;
    return unimplemented(ERR_SITE());
}

Ex<bool> Zynq7000Programmer::program_end() { return unimplemented(ERR_SITE()); }
Ex<bool> Zynq7000Programmer::program_step() { return unimplemented(ERR_SITE()); }

bool Zynq7000Programmer::programmed() const { return false; }

}  // namespace mister::boards
