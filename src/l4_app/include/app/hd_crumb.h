// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/fixed_str.h"

namespace mister::app {

[[nodiscard]] FixedStr<48, StrFit::Clip> hd_crumb(std::string_view core_name,
                                                  std::string_view title);

[[nodiscard]] std::string_view hd_crumb_core(std::string_view crumb) noexcept;

}  // namespace mister::app
