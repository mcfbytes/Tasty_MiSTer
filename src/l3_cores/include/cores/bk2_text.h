// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/digest_value.h"
#include "infra/error.h"

namespace mister::cores {

struct Bk2Line {
    enum class Kind : std::uint8_t { Blank, Long, LogKey, Pair };
    Kind kind = Kind::Blank;
    std::string_view key;
    std::string_view value;
};

[[nodiscard]] Ex<Bk2Line> bk2_read_line(std::string_view raw) noexcept;

[[nodiscard]] bool bk2_starts_log(std::string_view line) noexcept;

[[nodiscard]] bool bk2_ends_log(std::string_view line) noexcept;

[[nodiscard]] bool bk2_version_ok(std::string_view value) noexcept;

[[nodiscard]] std::optional<bool> bk2_boolean(std::string_view v) noexcept;

[[nodiscard]] Ex<std::optional<DigestValue>> bk2_game_db_digest(
    std::string_view key,
    std::string_view value,
    bool sha1_seen
) noexcept;

[[nodiscard]] Ex<std::optional<DigestValue>> bk2_prefixed_digest(
    std::string_view key,
    std::string_view value,
    bool have_digest
) noexcept;

}
