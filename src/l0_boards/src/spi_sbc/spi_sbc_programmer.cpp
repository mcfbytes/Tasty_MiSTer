// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/spi_sbc/spi_sbc_programmer.h"

namespace mister::boards {

Ex<bool> SpiSbcProgrammer::program_begin() { return unimplemented(ERR_SITE()); }

Ex<void> SpiSbcProgrammer::program_chunk(std::span<const std::byte> chunk) {
    (void)chunk;
    return unimplemented(ERR_SITE());
}

Ex<bool> SpiSbcProgrammer::program_end() { return unimplemented(ERR_SITE()); }
Ex<bool> SpiSbcProgrammer::program_step() { return unimplemented(ERR_SITE()); }

bool SpiSbcProgrammer::programmed() const { return false; }

}  // namespace mister::boards
