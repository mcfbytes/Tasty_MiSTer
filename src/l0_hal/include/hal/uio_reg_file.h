// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct UioRegFile {
    enum class Reg : std::uint32_t {};
    static constexpr std::size_t kSize = 0x40000;

    static constexpr Reg at(std::uint8_t op, std::uint8_t idx) noexcept {
        return static_cast<Reg>(
            (static_cast<std::uint32_t>(op) << 10) | (static_cast<std::uint32_t>(idx) << 2)
        );
    }
};

static_assert(
    static_cast<std::size_t>(UioRegFile::at(0xFF, 0xFF)) == UioRegFile::kSize - 4,
    "every (opcode, index) pair lands inside the block"
);

}
