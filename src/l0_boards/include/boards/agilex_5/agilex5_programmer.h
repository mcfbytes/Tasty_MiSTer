// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"
#include "hal/fpga_programmer.h"

namespace mister::boards {

class Agilex5Programmer final : public hal::IFpgaProgrammer {
public:
    Agilex5Programmer() = default;

    [[nodiscard]] Ex<bool> program_begin() override;
    [[nodiscard]] Ex<void> program_chunk(std::span<const std::byte> chunk) override;
    [[nodiscard]] Ex<bool> program_end() override;
    [[nodiscard]] Ex<bool> program_step() override;

    bool programmed() const override;
};

}  // namespace mister::boards
