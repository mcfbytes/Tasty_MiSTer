// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"
#include "hal/fpga_programmer.h"
#include "infra/prelude.h"

namespace mister::boards {

class RefusingProgrammer final : public hal::IFpgaProgrammer {
public:
    RefusingProgrammer() = default;

    [[nodiscard]] Ex<bool> program_begin() override;
    [[nodiscard]] Ex<void> program_chunk(std::span<const std::byte> chunk) override;
    [[nodiscard]] Ex<bool> program_end() override;
    [[nodiscard]] Ex<bool> program_step() override;

    bool programmed() const override;
};

}
