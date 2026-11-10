// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace mister::cores {

enum class DigestKind : std::uint8_t { Md5, Sha1, Sha256, Crc32 };

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

struct DigestValue {
    DigestKind kind = DigestKind::Md5;
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] constexpr std::size_t size() const noexcept { return digest_len(kind); }

    [[nodiscard]] constexpr std::span<const std::uint8_t> view() const noexcept {
        return {bytes.data(), size()};
    }

    friend bool operator==(const DigestValue&, const DigestValue&) = default;
};

[[nodiscard]] std::optional<DigestValue> parse_hex_digest(
    std::string_view hex,
    DigestKind kind
) noexcept;

}
