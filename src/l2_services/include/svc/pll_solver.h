// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "svc/pll_block.h"

namespace mister::svc {

struct PllParams;

namespace pll {

std::uint32_t encode_div(std::uint32_t div);

std::optional<PllParams> find(double f_out_mhz);

PllParams approximate(double f_out_mhz);

PllBlock block(const PllParams& p);

std::optional<PllParams> solve(double f_out_mhz);

double fpix_mhz(double f_out_mhz);

}

}
