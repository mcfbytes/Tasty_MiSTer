// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace mister::svc {

struct PllParams;

using PllBlock = std::array<std::uint32_t, 12>;

class PllSolver {
public:
    static std::uint32_t encode_div(std::uint32_t div);

    static std::optional<PllParams> find(double f_out_mhz);

    static PllParams approximate(double f_out_mhz);

    static PllBlock block(const PllParams& p);

    static std::optional<PllParams> solve(double f_out_mhz);

    static double fpix_mhz(double f_out_mhz);
};

}  // namespace mister::svc
