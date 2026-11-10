// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/endian.h"

namespace mister::cores {

struct QControl {
    static constexpr std::uint8_t kData = 4;
    static constexpr std::uint8_t kPre = 0x1;
    static constexpr std::uint8_t kDcp = 0x2;
    static constexpr std::uint8_t kFourChannel = 0x8;

    static constexpr std::uint8_t kTocData = 0x41;
    static constexpr std::uint8_t kTocAudio = 0x01;
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr QControl toc(bool data) noexcept {
        return QControl{data ? kTocData : kTocAudio};
    }

    [[nodiscard]] constexpr QControl with_data() const noexcept {
        return QControl{static_cast<std::uint8_t>(v | kData)};
    }

    [[nodiscard]] constexpr QControl with_pre() const noexcept {
        return QControl{static_cast<std::uint8_t>(v | kPre)};
    }

    [[nodiscard]] constexpr QControl with_dcp() const noexcept {
        return QControl{static_cast<std::uint8_t>(v | kDcp)};
    }

    [[nodiscard]] constexpr QControl with_four_channel() const noexcept {
        return QControl{static_cast<std::uint8_t>(v | kFourChannel)};
    }

    [[nodiscard]] constexpr bool is_data() const noexcept { return (v & kData) != 0; }

    constexpr void store(std::span<std::uint8_t, 2> dst) const noexcept {
        infra::store_be<std::uint16_t>(dst, v);
    }

    constexpr void store_le32(std::span<std::uint8_t, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    friend constexpr bool operator==(QControl, QControl) = default;
};

}
