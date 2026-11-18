// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

struct UioRegFile {
    enum class Reg : std::uint32_t {};

    enum class UioOp : std::uint8_t {};
    enum class UioIndex : std::uint8_t {};
    static constexpr std::size_t kSize = 0x40000;

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;

    enum Bits : std::uint32_t { kOpShift = 10, kIndexShift = 2 };

    [[nodiscard]] static constexpr Reg at(UioOp op, UioIndex idx) noexcept {
        return static_cast<Reg>(
            (static_cast<std::uint32_t>(op) << kOpShift)
            | (static_cast<std::uint32_t>(idx) << kIndexShift)
        );
    }
};

static_assert(
    static_cast<std::size_t>(UioRegFile::at(UioRegFile::UioOp{0xFF}, UioRegFile::UioIndex{0xFF}))
        == UioRegFile::kSize - 4,
    "every (opcode, index) pair lands inside the block"
);

}
