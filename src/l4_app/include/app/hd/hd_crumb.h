// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/fixed_str.h"
#include "infra/prelude.h"

namespace mister::app::hd {

[[nodiscard]] FixedStr<48, StrFit::Clip> hd_crumb(
    std::string_view core_name,
    std::string_view title
);

[[nodiscard]] std::string_view hd_crumb_core(std::string_view crumb) noexcept;

}
