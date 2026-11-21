// SPDX-License-Identifier: GPL-3.0-or-later
#include "boards/refusing_programmer.h"

#include "infra/prelude.h"

namespace mister::boards {

Ex<bool> RefusingProgrammer::program_begin() {
    return unimplemented(ERR_SITE());
}

Ex<void> RefusingProgrammer::program_chunk(std::span<const std::byte> chunk) {
    (void)chunk;
    return unimplemented(ERR_SITE());
}

Ex<bool> RefusingProgrammer::program_end() {
    return unimplemented(ERR_SITE());
}

Ex<bool> RefusingProgrammer::program_step() {
    return unimplemented(ERR_SITE());
}

bool RefusingProgrammer::programmed() const {
    return false;
}

}
