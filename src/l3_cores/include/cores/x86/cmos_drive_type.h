// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace mister::cores::x86 {

struct CmosDriveType {
    std::byte v{};

    static const CmosDriveType kNone;
    static const CmosDriveType k360K;
    static const CmosDriveType k1200K;
    static const CmosDriveType k720K;
    static const CmosDriveType k1440K;
    static const CmosDriveType k2880K;
    static const CmosDriveType kNamedHdd;
    static const CmosDriveType kExtendedHdd;

    [[nodiscard]] constexpr std::byte byte() const noexcept { return v; }

    friend constexpr bool operator==(CmosDriveType, CmosDriveType) = default;
};

inline constexpr CmosDriveType CmosDriveType::kNone{std::byte{0x0}};
inline constexpr CmosDriveType CmosDriveType::k360K{std::byte{0x1}};
inline constexpr CmosDriveType CmosDriveType::k1200K{std::byte{0x2}};
inline constexpr CmosDriveType CmosDriveType::k720K{std::byte{0x3}};
inline constexpr CmosDriveType CmosDriveType::k1440K{std::byte{0x4}};
inline constexpr CmosDriveType CmosDriveType::k2880K{std::byte{0x5}};
inline constexpr CmosDriveType CmosDriveType::kNamedHdd{std::byte{0xF}};
inline constexpr CmosDriveType CmosDriveType::kExtendedHdd{std::byte{0x2F}};

struct CmosDrivePair {
    CmosDriveType first{};
    CmosDriveType second{};

    [[nodiscard]] constexpr std::byte byte() const noexcept {
        constexpr unsigned kNibbleBits = 4;
        return (first.byte() << kNibbleBits) | second.byte();
    }

    friend constexpr bool operator==(CmosDrivePair, CmosDrivePair) = default;
};

}
