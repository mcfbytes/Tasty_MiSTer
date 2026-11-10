// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/post_notify.h"
#include "proto/types.h"

namespace mister::cores::neogeo {

enum class FileClass : std::uint8_t { Raw, Fix, Spr };

struct FileIndex {
    std::uint8_t v = 0;

    static const FileIndex Bios;
    static const FileIndex LoRom;
    static const FileIndex Sfix;
    static const FileIndex Prom;
    static const FileIndex Prom2;
    static const FileIndex Srom;
    static const FileIndex M1;
    static const FileIndex Sprite;
    static const FileIndex VromFirst;
    static const FileIndex Fill;
    static const FileIndex VromB;
    static const FileIndex CromFirst;

    [[nodiscard]] static constexpr FileIndex of(int value) noexcept {
        return FileIndex{static_cast<std::uint8_t>(value)};
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
        return static_cast<std::uint32_t>(v - VromFirst.v) * kStrideBytes;
    }

    [[nodiscard]] constexpr std::uint32_t mirror_stride() const noexcept {
        constexpr std::uint32_t kWindowMask = 0xFF'FFFFu;
        return vrom_offset() & kWindowMask;
    }

    [[nodiscard]] constexpr std::uint32_t crom_offset() const noexcept {
        constexpr std::uint32_t kPairBytes = 0x10'0000u;
        const std::uint32_t pair = static_cast<std::uint32_t>(v - CromFirst.v) >> 1u;
        return pair * kPairBytes;
    }

    [[nodiscard]] constexpr std::uint8_t crom_lane() const noexcept {
        return static_cast<std::uint8_t>((v ^ 1u) & 1u);
    }

    [[nodiscard]] constexpr FileClass class_of() const noexcept {
        if (at_or_above(CromFirst) || *this == Sprite)
            return FileClass::Spr;
        if (*this == Sfix || *this == Srom)
            return FileClass::Fix;
        return FileClass::Raw;
    }

    [[nodiscard]] constexpr bool is_crom() const noexcept { return at_or_above(CromFirst); }

    [[nodiscard]] constexpr bool is_vrom() const noexcept {
        return at_or_above(VromFirst) && below(CromFirst);
    }

    [[nodiscard]] constexpr bool is_prom() const noexcept {
        return *this == Prom || *this == Prom2;
    }

    [[nodiscard]] constexpr bool in_pcm_range() const noexcept {
        return at_or_above(VromB) && below(CromFirst);
    }

    [[nodiscard]] constexpr std::uint8_t vrom_fill() const noexcept {
        constexpr std::uint8_t kVromFill = 8;
        return is_vrom() ? kVromFill : 0u;
    }

private:
    [[nodiscard]] constexpr bool at_or_above(FileIndex slot) const noexcept { return v >= slot.v; }

    [[nodiscard]] constexpr bool below(FileIndex slot) const noexcept { return v < slot.v; }
};

inline constexpr std::uint32_t kVromSize = 16u * 1024u * 1024u;

inline constexpr FileIndex FileIndex::Bios{0};
inline constexpr FileIndex FileIndex::LoRom{1};
inline constexpr FileIndex FileIndex::Sfix{2};
inline constexpr FileIndex FileIndex::Prom{4};
inline constexpr FileIndex FileIndex::Prom2{6};
inline constexpr FileIndex FileIndex::Srom{8};
inline constexpr FileIndex FileIndex::M1{9};
inline constexpr FileIndex FileIndex::Sprite{15};
inline constexpr FileIndex FileIndex::VromFirst{16};
inline constexpr FileIndex FileIndex::Fill{18};
inline constexpr FileIndex FileIndex::VromB{48};
inline constexpr FileIndex FileIndex::CromFirst{64};
static_assert(sizeof(FileIndex) == 1);

}
