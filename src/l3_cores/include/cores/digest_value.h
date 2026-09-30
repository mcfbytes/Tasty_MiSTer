// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mister::cores {

enum class DigestKind : std::uint8_t { Md5, Sha1, Sha256, Crc32 };

struct DigestValue {
    DigestKind kind = DigestKind::Md5;
    std::uint8_t len = 0;
    std::array<std::uint8_t, 32> bytes{};
    friend bool operator==(const DigestValue&, const DigestValue&) = default;
};

[[nodiscard]] constexpr std::uint8_t digest_len(DigestKind k) noexcept {
    switch (k) {
        case DigestKind::Md5:
            return 16;
        case DigestKind::Sha1:
            return 20;
        case DigestKind::Sha256:
            return 32;
        case DigestKind::Crc32:
            return 4;
    }
    return 0;
}

[[nodiscard]] std::optional<DigestValue> parse_hex_digest(std::string_view hex,
                                                          DigestKind kind) noexcept;

}  // namespace mister::cores
