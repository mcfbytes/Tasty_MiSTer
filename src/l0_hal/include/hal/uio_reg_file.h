// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct UioRegFile {
    enum class Reg : std::uint32_t {};

    enum class UioOp : std::uint8_t {};
    enum class UioIndex : std::uint8_t {};
    static constexpr std::size_t kSize = 0x40000;

    [[nodiscard]] static constexpr std::uint32_t offset_of(UioOp op, UioIndex idx) noexcept {
        return (static_cast<std::uint32_t>(op) << 10) | (static_cast<std::uint32_t>(idx) << 2);
    }

    static constexpr Reg at(UioOp op, UioIndex idx) noexcept {
        return static_cast<Reg>(offset_of(op, idx));
    }
};

static_assert(
    static_cast<std::size_t>(UioRegFile::at(UioRegFile::UioOp{0xFF}, UioRegFile::UioIndex{0xFF}))
        == UioRegFile::kSize - 4,
    "every (opcode, index) pair lands inside the block"
);

}
