// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/post_notify.h"
#include "proto/types.h"

namespace mister::cores::neogeo {

enum class FileClass : std::uint8_t { Raw, EightBit, Fix, Spr };

struct FileIndex {
    enum Id : std::uint8_t {
        Bios = 0,
        LoRom = 1,
        Sfix = 2,
        Prom = 4,
        Prom2 = 6,
        Srom = 8,
        M1 = 9,
        Sprite = 15,
        VromFirst = 16,
        Fill = 18,
        VromB = 48,
        CromFirst = 64,
    };

    std::uint8_t v = 0;

    constexpr FileIndex() noexcept = default;

    constexpr FileIndex(Id id) noexcept : v(static_cast<std::uint8_t>(id)) {}

    [[nodiscard]] static constexpr FileIndex of(int value) noexcept {
        FileIndex index{};
        index.v = static_cast<std::uint8_t>(value);
        return index;
    }

    friend constexpr bool operator==(FileIndex, FileIndex) = default;

    [[nodiscard]] constexpr PostNotify notify_words(std::uint32_t size, bool memcp) const noexcept {
        return PostNotify::sized(static_cast<std::uint16_t>(v), size, memcp);
    }

    [[nodiscard]] constexpr proto::WideIoIndex wide() const noexcept {
        return proto::WideIoIndex{static_cast<std::uint16_t>(v)};
    }

    [[nodiscard]] constexpr std::uint32_t vrom_offset() const noexcept {
        constexpr std::uint32_t kStrideBytes = 0x8'0000u;
        return static_cast<std::uint32_t>(v - VromFirst) * kStrideBytes;
    }

    [[nodiscard]] constexpr std::uint32_t mirror_stride() const noexcept {
        constexpr std::uint32_t kWindowMask = 0xFF'FFFFu;
        return vrom_offset() & kWindowMask;
    }

    [[nodiscard]] constexpr std::uint32_t crom_offset() const noexcept {
        constexpr std::uint32_t kPairBytes = 0x10'0000u;
        const std::uint32_t pair = static_cast<std::uint32_t>(v - CromFirst) >> 1u;
        return pair * kPairBytes;
    }

    [[nodiscard]] constexpr std::uint8_t crom_lane() const noexcept {
        return static_cast<std::uint8_t>((v ^ 1u) & 1u);
    }

    [[nodiscard]] constexpr FileClass class_of() const noexcept {
        if (v >= CromFirst || v == Sprite)
            return FileClass::Spr;
        if (v == Sfix || v == Srom)
            return FileClass::Fix;
        return FileClass::Raw;
    }

    [[nodiscard]] constexpr bool is_crom() const noexcept { return v >= CromFirst; }

    [[nodiscard]] constexpr bool is_vrom() const noexcept {
        return v >= VromFirst && v < CromFirst;
    }

    [[nodiscard]] constexpr bool is_prom() const noexcept { return v == Prom || v == Prom2; }

    [[nodiscard]] constexpr bool in_pcm_range() const noexcept {
        return v >= VromB && v < CromFirst;
    }

    [[nodiscard]] constexpr std::uint8_t vrom_fill() const noexcept {
        constexpr std::uint8_t kVromFill = 8;
        return is_vrom() ? kVromFill : 0u;
    }
};

static_assert(sizeof(FileIndex) == 1);

}
