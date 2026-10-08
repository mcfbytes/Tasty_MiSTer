// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace mister::svc {

struct PllParams;

using PllBlock = std::array<std::uint32_t, 12>;

namespace pll {

std::uint32_t encode_div(std::uint32_t div);

std::optional<PllParams> find(double f_out_mhz);

PllParams approximate(double f_out_mhz);

PllBlock block(const PllParams& p);

std::optional<PllParams> solve(double f_out_mhz);

double fpix_mhz(double f_out_mhz);

}  // namespace pll

}  // namespace mister::svc
