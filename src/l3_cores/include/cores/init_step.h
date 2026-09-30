// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "proto/core_session.h"

namespace mister::cores {

constexpr CoreTypeMask dialect_bit(proto::CoreType t) noexcept {
    switch (t) {
        case proto::CoreType::EightBit:
            return CoreTypeMask{0x01};
        case proto::CoreType::SharpMz:
            return CoreTypeMask{0x02};
        case proto::CoreType::Unknown:
            return CoreTypeMask{0x04};
    }
    return CoreTypeMask{};
}
inline constexpr CoreTypeMask kEightBitOnly{0x01};

}  // namespace mister::cores
