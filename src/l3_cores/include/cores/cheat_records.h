// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/cheat_geometry.h"

namespace mister::cores {

class ICheatRecords {
public:
    virtual ~ICheatRecords() = default;

    [[nodiscard]] virtual CheatGeometry cheat_geometry() const noexcept = 0;
    [[nodiscard]] virtual std::size_t cheat_count() const noexcept = 0;
    [[nodiscard]] virtual std::string_view cheat_name(std::size_t i) const noexcept = 0;
    [[nodiscard]] virtual std::span<const std::uint8_t> cheat_bytes(
        std::size_t i) const noexcept = 0;

protected:
    ICheatRecords() = default;
    ICheatRecords(const ICheatRecords&) = default;
    ICheatRecords& operator=(const ICheatRecords&) = default;
};

}  // namespace mister::cores
