// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores {

struct PostNotify {
    static constexpr std::uint8_t kIndex = 10;
    static constexpr std::size_t kWords = 5;

    static constexpr std::uint16_t kNoCopyBit = 0x8000;

    static constexpr unsigned kWordBits = 16;
    static constexpr std::size_t kSizeLowWord = 1;
    static constexpr std::size_t kSizeHighWord = 2;

    static constexpr std::uint8_t kCopyFlagWord = 4;

    bool armed = false;
    std::array<std::uint16_t, kWords> words{};

private:
    using Halves = std::array<std::uint16_t, 2>;

    [[nodiscard]] static constexpr Halves split_(std::uint32_t word) noexcept {
        std::array<std::uint8_t, sizeof(std::uint32_t)> bytes{};
        infra::store_le<std::uint32_t>(std::span<std::uint8_t, sizeof(std::uint32_t)>(bytes), word);
        const std::span<const std::uint8_t> view(bytes);
        return {
            infra::load_le<std::uint16_t>(view.first<sizeof(std::uint16_t)>()),
            infra::load_le<std::uint16_t>(view.last<sizeof(std::uint16_t)>())
        };
    }

public:
    [[nodiscard]] static constexpr PostNotify sized(
        std::uint16_t announce,
        std::uint32_t size,
        bool memcp
    ) noexcept {
        const Halves half = split_(size);
        return PostNotify{
            .armed = true,
            .words = {announce, half[0], half[1], std::uint16_t{memcp}, 0u}
        };
    }

    [[nodiscard]] static constexpr PostNotify config(std::uint32_t conf) noexcept {
        const Halves half = split_(conf);
        return PostNotify{.armed = true, .words = {kNoCopyBit, half[0], half[1], 0u, 0u}};
    }

    [[nodiscard]] constexpr bool copies() const noexcept {
        return armed && (words[0] & kNoCopyBit) == 0u && words[kCopyFlagWord - 1u] != 0u;
    }

    [[nodiscard]] constexpr std::uint8_t copy_word() const noexcept {
        return copies() ? kCopyFlagWord : std::uint8_t{0};
    }

    [[nodiscard]] constexpr proto::FileSize copy_bytes() const noexcept {
        if (!copies())
            return proto::FileSize{};
        return proto::FileSize{
            words[kSizeLowWord] | (std::uint32_t{words[kSizeHighWord]} << kWordBits)
        };
    }
};

}
