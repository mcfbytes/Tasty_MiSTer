// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/n64dd_geometry.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::cores::n64dd {

inline constexpr std::size_t kSignatureBytes = 4;
inline constexpr std::size_t kFormatOffset = 4;
inline constexpr std::size_t kTypeOffset = 5;

inline constexpr std::size_t kDefectTableOffset = 0x07;

inline constexpr std::size_t kDefectListOffset = 0x20;
inline constexpr std::size_t kFillerOffset = 0x18;
inline constexpr std::size_t kFillerBytes = 4;
inline constexpr std::size_t kTerminatorOffset = 0x1c;

inline constexpr std::uint8_t kFormat = 0x10;
inline constexpr std::uint8_t kTypeBase = 0x10;
inline constexpr std::uint8_t kTypeMask = 0x0f;
inline constexpr std::uint8_t kFillerByte = 0xff;
inline constexpr std::uint8_t kTerminator = 0x80;

using Signature = std::array<std::uint8_t, kSignatureBytes>;
inline constexpr Signature kRetailJpnSig{0xE8, 0x48, 0xD3, 0x16};
inline constexpr Signature kRetailUsaSig{0x22, 0x63, 0xEE, 0x56};
inline constexpr Signature kDevSig{0x00, 0x00, 0x00, 0x00};

[[nodiscard]] constexpr bool signature_is(
    std::span<const std::uint8_t, kSignatureBytes> bytes,
    const Signature& sig
) noexcept {
    for (std::size_t i = 0; i < kSignatureBytes; ++i) {
        if (bytes[i] != sig[i])
            return false;
    }
    return true;
}

struct DiskType {
    std::uint8_t v = 0;

    static constexpr std::uint8_t kCount = static_cast<std::uint8_t>(kDiskTypes);

    [[nodiscard]] static constexpr DiskType of_byte(std::uint8_t byte) noexcept {
        return DiskType{static_cast<std::uint8_t>(byte & kTypeMask)};
    }

    [[nodiscard]] constexpr bool is_known() const noexcept { return v < kCount; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(DiskType, DiskType) = default;
};

struct SystemBlockView {
    std::span<const std::uint8_t> v{};

    [[nodiscard]] constexpr std::span<const std::uint8_t, kSignatureBytes> magic() const noexcept {
        return v.first<kSignatureBytes>();
    }

    [[nodiscard]] constexpr bool is(const Signature& sig) const noexcept {
        return signature_is(magic(), sig);
    }

    [[nodiscard]] constexpr std::uint8_t format() const noexcept { return v[kFormatOffset]; }

    [[nodiscard]] constexpr std::uint8_t type_byte() const noexcept { return v[kTypeOffset]; }

    [[nodiscard]] constexpr DiskType type() const noexcept {
        return DiskType::of_byte(type_byte());
    }

    [[nodiscard]] constexpr bool type_in_range() const noexcept {
        return type_byte() >= kTypeBase
            && type_byte() < static_cast<std::uint8_t>(kTypeBase + DiskType::kCount);
    }

    [[nodiscard]] constexpr std::uint8_t defect_start(std::uint32_t pzone) const noexcept {
        return v[kDefectTableOffset + pzone];
    }

    [[nodiscard]] constexpr std::uint8_t defect_stop(std::uint32_t pzone) const noexcept {
        return v[kDefectTableOffset + pzone + 1u];
    }

    [[nodiscard]] constexpr std::uint8_t defect_entry(std::uint32_t d) const noexcept {
        return v[kDefectListOffset + d];
    }

    [[nodiscard]] constexpr bool has_defect_entry(std::uint32_t d) const noexcept {
        return kDefectListOffset + d < v.size();
    }

    [[nodiscard]] constexpr bool filler() const noexcept {
        for (std::size_t i = 0; i < kFillerBytes; ++i) {
            if (v[kFillerOffset + i] != kFillerByte)
                return false;
        }
        return true;
    }

    [[nodiscard]] constexpr std::uint8_t terminator() const noexcept {
        return v[kTerminatorOffset];
    }
};

}
