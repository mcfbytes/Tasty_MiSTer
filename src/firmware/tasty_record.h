// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "app/path_text.h"
#include "infra/error.h"
#include "tasty_cli.h"
#include "tasty_writeback.h"

namespace mister::fw {

[[nodiscard]] Ex<TastyArgs> tasty_resolve_args(TastyArgs args) noexcept;

[[nodiscard]] std::string_view tasty_record_dir(std::string_view record) noexcept;

[[nodiscard]] Ex<app::PathText> tasty_prepare_record(
    app::PathText record,
    std::string_view movie
) noexcept;

}
