// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/save_banks.h"

namespace mister::cores::c64 {

inline constexpr std::uint8_t kErased = std::to_integer<std::uint8_t>(svc::kErasedFlash);

inline constexpr proto::FileSize kCrtHeaderBytes{64};
inline constexpr proto::FileSize kChipHeadBytes{16};
inline constexpr proto::FileSize kChipBodyBytes{8192};
inline constexpr proto::FileSize kChipPacketBytes = kChipHeadBytes + kChipBodyBytes;

inline constexpr std::uint64_t kEasyFlashPackets = 128;

inline constexpr proto::FileSize kEasyFlashImageBytes = kCrtHeaderBytes
    + kChipPacketBytes * kEasyFlashPackets;

inline constexpr proto::FileSize kEasyFlashWireBytes = kChipBodyBytes * kEasyFlashPackets;

inline constexpr proto::WideIoIndex kEasyFlashUploadIndex{99};

inline constexpr svc::SaveBanks kEasyFlashBanks{
    .lead = kCrtHeaderBytes,
    .head = kChipHeadBytes,
    .body = kChipBodyBytes
};

using CrtHeader = std::array<std::uint8_t, kCrtHeaderBytes.file_word<std::size_t>()>;
using ChipHead = std::array<std::uint8_t, kChipHeadBytes.file_word<std::size_t>()>;
using ChipPacket = std::array<std::uint8_t, kChipPacketBytes.file_word<std::size_t>()>;
using EasyFlashHeads = std::
    array<std::uint8_t, (kChipHeadBytes * kEasyFlashPackets).file_word<std::size_t>()>;

namespace detail {

inline constexpr ChipHead kChipHead{
    0x43,
    0x48,
    0x49,
    0x50,
    0x00,
    0x00,
    0x20,
    0x10,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x00,
    0x20,
    0x00
};

inline constexpr std::size_t kChipBankAt = 11;
inline constexpr std::size_t kChipPageAt = 12;

inline constexpr std::uint8_t kRomlPage = 0x80;
inline constexpr std::uint8_t kRomhPage = 0xA0;

[[nodiscard]] constexpr EasyFlashHeads synthesized_heads() noexcept {
    EasyFlashHeads out{};
    std::uint8_t bank = 0;
    bool high = false;
    for (auto at = out.begin(); at != out.end(); at += kChipHead.size()) {
        ChipHead head = kChipHead;
        head[kChipBankAt] = bank;
        head[kChipPageAt] = high ? kRomhPage : kRomlPage;
        for (std::size_t i = 0; i < head.size(); ++i)
            at[i] = head[i];
        if (high)
            ++bank;
        high = !high;
    }
    return out;
}

}

inline constexpr EasyFlashHeads kEasyFlashHeads = detail::synthesized_heads();

class EasyFlashImage {
    TASTY_SEAT_RESIDENT(Main);

public:
    [[nodiscard]] static bool named_by(const CrtHeader& header) noexcept;

    explicit EasyFlashImage(const CrtHeader& header);

    [[nodiscard]] bool place(const ChipPacket& packet) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

    [[nodiscard]] std::vector<std::uint8_t> release() && noexcept;

private:
    std::vector<std::uint8_t> bytes_;
};

}
