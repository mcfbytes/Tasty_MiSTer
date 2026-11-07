// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "proto/core_session.h"

namespace mister::cores {

inline constexpr CoreTypeMask kEightBitDialect{0x01};
inline constexpr CoreTypeMask kSharpMzDialect{0x02};
inline constexpr CoreTypeMask kUnknownDialect{0x04};

constexpr CoreTypeMask dialect_bit(proto::CoreType t) noexcept {
    switch (t) {
        case proto::CoreType::EightBit:
            return kEightBitDialect;
        case proto::CoreType::SharpMz:
            return kSharpMzDialect;
        case proto::CoreType::Unknown:
            return kUnknownDialect;
    }
    return CoreTypeMask{};
}

}
