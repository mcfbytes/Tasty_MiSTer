// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ui/page_row.h"

namespace mister::ui {

[[nodiscard]] PageRow::Classic classic_row(
    proto::OsdRow row,
    std::string_view text,
    bool inverted = false,
    bool stippled = false,
    std::optional<Glyph> leftchar = std::nullopt,
    std::optional<OsdRenderer::Arrow> arrow = std::nullopt
) noexcept;

}
