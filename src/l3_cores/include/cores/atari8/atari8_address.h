// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/byte_count.h"
#include "proto/aux_value.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"

namespace mister::cores::atari8 {

struct Atari8Address {
    std::uint32_t v = 0;

    static const Atari8Address kHostMemory;
    static const Atari8Address kSdram;

    [[nodiscard]] constexpr Atari8Address after(infra::ByteCount n) const noexcept {
        return Atari8Address{v + static_cast<std::uint32_t>(n.v)};
    }

    [[nodiscard]] constexpr Atari8Address next_block() const noexcept {
        return Atari8Address{v + std::uint32_t{proto::kMailboxActBytes}};
    }

    [[nodiscard]] constexpr Atari8Address advanced(proto::FileSize bytes) const noexcept {
        return Atari8Address{v + bytes.low_word()};
    }

    [[nodiscard]] constexpr proto::AuxValue aux() const noexcept { return proto::AuxValue{v}; }

    friend constexpr bool operator==(Atari8Address, Atari8Address) = default;
    friend constexpr auto operator<=>(Atari8Address, Atari8Address) = default;
};

inline constexpr Atari8Address Atari8Address::kHostMemory{0x0001'0000};
inline constexpr Atari8Address Atari8Address::kSdram{0x0200'0000};

}
