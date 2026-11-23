// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "proto/bit_field.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores {

enum class HostFact : std::uint8_t {
    SaveAutomountInhibit,
    StartRomInhibit,
    SaveUploadTrigger,
    RamSize,
};

struct HostBitRow {

    enum class Dir : std::uint8_t { Read, Pulse };
    HostFact fact{};
    proto::BitField bits{};
    Dir dir = Dir::Read;
};

[[nodiscard]] constexpr proto::FieldValue host_value(
    const proto::StatusWord& s,
    std::span<const HostBitRow> rows,
    HostFact f
) noexcept {
    for (const HostBitRow& r : rows)
        if (r.fact == f && r.dir == HostBitRow::Dir::Read)
            return s.field(r.bits);
    return proto::FieldValue{};
}

[[nodiscard]] constexpr bool host_pulses(
    std::span<const HostBitRow> rows,
    HostFact f,
    proto::StatusBit bit
) noexcept {
    for (const HostBitRow& r : rows)
        if (r.fact == f && r.dir == HostBitRow::Dir::Pulse)
            return r.bits.holds(bit);
    return false;
}

}
