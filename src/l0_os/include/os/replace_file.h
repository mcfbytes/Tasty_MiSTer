// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/file_mode.h"
#include "infra/prelude.h"
#include "infra/rename_files.h"

namespace mister::os {

enum class ReplaceSync : std::uint8_t { None, File };

[[nodiscard]] Ex<void> replace_file(
    const infra::RenameFiles& names,
    std::span<const std::byte> bytes,
    infra::FileMode mode,
    ReplaceSync durability
) noexcept;

[[nodiscard]] Ex<void> replace_file(
    const infra::RenameFiles& names,
    std::string_view text,
    infra::FileMode mode,
    ReplaceSync durability
) noexcept;

}
