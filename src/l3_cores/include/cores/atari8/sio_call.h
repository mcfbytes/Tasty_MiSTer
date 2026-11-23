// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/sio_command.h"
#include "infra/byte_count.h"
#include "proto/mailbox_act.h"

namespace mister::cores::atari8 {

enum class SioDirection : std::uint8_t { None = 0x00, FromDevice = 0x40, ToDevice = 0x80 };

[[nodiscard]] constexpr SioDirection direction_of(std::byte dstats) noexcept {
    for (const SioDirection d : {SioDirection::FromDevice, SioDirection::ToDevice})
        if (dstats == std::byte{std::to_underlying(d)})
            return d;
    return SioDirection::None;
}

struct SioCall {
    SioBus bus = SioBus::Serial;

    SioDirection direction = SioDirection::None;

    infra::ByteCount length{};

    std::span<const std::byte> host{};
    Atari8Status status{};
    proto::MailboxAct::NotBefore now{};

    [[nodiscard]] constexpr bool serial() const noexcept { return bus == SioBus::Serial; }

    [[nodiscard]] constexpr bool reads() const noexcept {
        return serial() || direction == SioDirection::FromDevice;
    }

    [[nodiscard]] constexpr bool writes() const noexcept {
        return serial() || direction == SioDirection::ToDevice;
    }
};

}
