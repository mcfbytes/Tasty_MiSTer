// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>

#include "infra/seat.h"

namespace mister::proto {

struct SlotIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(SlotIndex, SlotIndex) = default;
    friend constexpr auto operator<=>(SlotIndex, SlotIndex) = default;
};

struct IoIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(IoIndex, IoIndex) = default;
    friend constexpr auto operator<=>(IoIndex, IoIndex) = default;
};

struct FileSlotDigit {
    std::uint8_t v = 0;
    friend constexpr bool operator==(FileSlotDigit, FileSlotDigit) = default;
};

struct ItemOrdinal {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ItemOrdinal, ItemOrdinal) = default;
};

struct WideIoIndex {
    std::uint16_t v = 0;
    friend constexpr bool operator==(WideIoIndex, WideIoIndex) = default;
};

struct PathId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(PathId, PathId) = default;
};

struct FileSize {
    std::uint64_t v = 0;
    friend constexpr bool operator==(FileSize, FileSize) = default;
    friend constexpr auto operator<=>(FileSize, FileSize) = default;
};

struct Lba {
    std::uint32_t v = 0;
    friend constexpr bool operator==(Lba, Lba) = default;
    friend constexpr auto operator<=>(Lba, Lba) = default;
};

struct BlockCount {
    std::uint32_t v = 0;
    friend constexpr bool operator==(BlockCount, BlockCount) = default;
    friend constexpr auto operator<=>(BlockCount, BlockCount) = default;
};

struct SdStatusWord {
    std::uint16_t v = 0;
    friend constexpr bool operator==(SdStatusWord, SdStatusWord) = default;
};

struct MemSizeCookie {
    std::uint16_t v = 0;
    friend constexpr bool operator==(MemSizeCookie, MemSizeCookie) = default;
};

struct StatusBit {
    std::uint8_t v = 0;
    friend constexpr bool operator==(StatusBit, StatusBit) = default;
    friend constexpr auto operator<=>(StatusBit, StatusBit) = default;
};

struct OsdRow {
    std::uint8_t v = 0;
    friend constexpr bool operator==(OsdRow, OsdRow) = default;
    friend constexpr auto operator<=>(OsdRow, OsdRow) = default;
};

struct HidUsage {
    std::uint8_t v = 0;
    friend constexpr bool operator==(HidUsage, HidUsage) = default;
};

struct KeyCode {
    std::uint32_t v = 0;
    friend constexpr bool operator==(KeyCode, KeyCode) = default;
};

struct PlayerIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(PlayerIndex, PlayerIndex) = default;
    friend constexpr auto operator<=>(PlayerIndex, PlayerIndex) = default;
};

struct JoyMask {
    std::uint32_t v = 0;
    friend constexpr bool operator==(JoyMask, JoyMask) = default;
};

struct OsdMask {
    std::uint16_t v = 0;
    friend constexpr bool operator==(OsdMask, OsdMask) = default;
};

struct StorageSeq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(StorageSeq, StorageSeq) = default;
};

struct BindGeneration {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t v = 0;
    friend constexpr bool operator==(BindGeneration, BindGeneration) = default;
};

struct ArenaHalf {
    std::uint8_t v = 0;
    friend constexpr bool operator==(ArenaHalf, ArenaHalf) = default;
};

struct RxSlabId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(RxSlabId, RxSlabId) = default;
};

struct TxSlabId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(TxSlabId, TxSlabId) = default;
};

struct FileId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(FileId, FileId) = default;
};

struct CorrelationTag {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CorrelationTag, CorrelationTag) = default;
    explicit constexpr operator bool() const noexcept { return v != 0; }
};
inline constexpr CorrelationTag kUncaused{0};

}  // namespace mister::proto
