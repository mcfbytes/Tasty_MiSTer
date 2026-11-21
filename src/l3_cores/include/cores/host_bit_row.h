// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "proto/bit_field.h"
#include "proto/status_word.h"
#include "proto/types.h"

namespace mister::cores {

enum class HostFact : std::uint8_t { SaveAutomountInhibit };

struct HostBitRow {
    enum class Dir : std::uint8_t { Read };
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
        if (r.fact == f)
            return s.field(r.bits);
    return proto::FieldValue{};
}

}
