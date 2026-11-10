// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/n64_memory_type.h"
#include "infra/endian.h"
#include "infra/error.h"

namespace mister::cores::n64::cpak {

inline constexpr std::size_t kPageSize = 256;
inline constexpr std::size_t kIdSectorPage = 0;
inline constexpr std::size_t kFatPage = 1;
inline constexpr std::size_t kFatBackupPage = 2;
inline constexpr std::size_t kIdEntrySize = 32;
inline constexpr std::array<std::size_t, 4>
    kIdEntryOffsets{kIdEntrySize * 1, kIdEntrySize * 3, kIdEntrySize * 4, kIdEntrySize * 6};

inline constexpr std::size_t kImageBytes = save_size(MemoryType::Cpak);
static_assert(kImageBytes == 0x8000);

inline constexpr std::size_t kSerialBytes = 0x10;
inline constexpr std::size_t kBrandOffset = 0x10;
inline constexpr std::string_view kBrand = "MiSTer";
inline constexpr std::size_t kDeviceIdOffset = 0x19;
inline constexpr std::size_t kBankCountOffset = 0x1A;
inline constexpr std::uint8_t kDeviceId = 0x81;
inline constexpr std::uint8_t kBankCount = 0x01;

inline constexpr std::uint16_t kIdChecksumBase = 0xFFF2;
inline constexpr std::size_t kIdChecksumHalfWords = 14;
inline constexpr std::size_t kIdSumOffset = 0x1C;
inline constexpr std::size_t kIdInverseOffset = 0x1E;

inline constexpr std::size_t kFatChecksumOffset = 1;
inline constexpr std::size_t kFatChecksumFrom = 2;
inline constexpr std::size_t kFatFirstUserEntry = 5;
inline constexpr std::uint8_t kFatFreeMark = 0x03;

using Serial = std::array<std::uint8_t, kSerialBytes>;
using IdEntry = std::array<std::uint8_t, kIdEntrySize>;
using Page = std::array<std::uint8_t, kPageSize>;

[[nodiscard]] constexpr std::uint16_t id_checksum(
    std::span<const std::uint8_t, kIdEntrySize> entry
) noexcept {
    std::uint16_t sum = 0;
    for (std::size_t i = 0; i < kIdChecksumHalfWords; ++i) {
        const auto hi = static_cast<unsigned>(entry[i * 2]);
        const auto lo = static_cast<unsigned>(entry[(i * 2) + 1]);
        sum = static_cast<std::uint16_t>(sum + ((hi << 8) | lo));
    }
    return sum;
}

[[nodiscard]] constexpr std::uint16_t id_inverse_checksum(std::uint16_t sum) noexcept {
    return static_cast<std::uint16_t>(kIdChecksumBase - sum);
}

[[nodiscard]] constexpr std::uint8_t fat_checksum(
    std::span<const std::uint8_t, kPageSize> page
) noexcept {
    std::uint8_t sum = 0;
    for (std::size_t i = kFatChecksumFrom; i < kPageSize; ++i) {
        sum = static_cast<std::uint8_t>(sum + page[i]);
    }
    return sum;
}

namespace detail {

consteval Page make_fat_page() {
    Page p{};
    for (std::size_t i = kFatFirstUserEntry; i < (kPageSize / 2); ++i) {
        p[(i * 2) + 1] = kFatFreeMark;
    }
    p[kFatChecksumOffset] = fat_checksum(std::span<const std::uint8_t, kPageSize>{p});
    return p;
}

consteval IdEntry make_id_template() {
    IdEntry e{};
    for (std::size_t i = 0; i < kBrand.size(); ++i) {
        e[kBrandOffset + i] = static_cast<std::uint8_t>(kBrand[i]);
    }
    e[kDeviceIdOffset] = kDeviceId;
    e[kBankCountOffset] = kBankCount;
    return e;
}

}

inline constexpr Page kFatPageImage = detail::make_fat_page();

inline constexpr IdEntry kIdTemplate = detail::make_id_template();

[[nodiscard]] constexpr IdEntry id_entry(
    std::span<const std::uint8_t, kSerialBytes> serial
) noexcept {
    IdEntry e = kIdTemplate;
    for (std::size_t i = 0; i < kSerialBytes; ++i)
        e[i] = serial[i];
    const std::uint16_t sum = id_checksum(std::span<const std::uint8_t, kIdEntrySize>{e});
    const std::uint16_t inv = id_inverse_checksum(sum);

    infra::store_be<std::uint16_t>(std::span<std::uint8_t>(e).subspan<kIdSumOffset, 2u>(), sum);
    infra::store_be<std::uint16_t>(std::span<std::uint8_t>(e).subspan<kIdInverseOffset, 2u>(), inv);
    return e;
}

constexpr void render(
    std::span<const std::uint8_t, kSerialBytes> serial,
    std::span<std::uint8_t, kImageBytes> image
) noexcept {
    for (std::uint8_t& b : image)
        b = 0;
    const IdEntry e = id_entry(serial);
    for (const std::size_t at : kIdEntryOffsets) {
        for (std::size_t i = 0; i < kIdEntrySize; ++i) {
            image[(kIdSectorPage * kPageSize) + at + i] = e[i];
        }
    }
    for (const std::size_t page : {kFatPage, kFatBackupPage}) {
        for (std::size_t i = 0; i < kPageSize; ++i) {
            image[(page * kPageSize) + i] = kFatPageImage[i];
        }
    }
}

[[nodiscard]] Ex<Serial> random_serial();

}
