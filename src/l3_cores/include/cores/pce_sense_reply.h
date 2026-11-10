// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores {

struct PceSenseReply {
    enum : std::size_t {
        kLength = 0,
        kControl = 1,
        kResponse = 2,
        kKey = 4,
        kAdditional = 9,
        kAsc = 14,
        kBytes = 20,
    };

    enum : std::uint8_t {
        kLengthValue = 18,
        kControlValue = 0x80,
        kResponseValue = 0x70,
        kAdditionalValue = 0x0A,
    };

    PceSenseReply() noexcept;
    void set_key(std::byte key) noexcept;
    void set_asc(std::byte asc) noexcept;
    [[nodiscard]] std::span<const std::uint8_t, kBytes> bytes() const noexcept;

private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

}
