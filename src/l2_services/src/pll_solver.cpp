// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/pll_solver.h"
#include "svc/pll_params.h"

#include "video_service_detail.h"

namespace mister::svc {

std::uint32_t pll::encode_div(std::uint32_t div) {

    if (div & 1u) {
        return 0x20000u | (((div / 2u) + 1u) << 8) | (div / 2u);
    }
    return ((div / 2u) << 8) | (div / 2u);
}

std::optional<PllParams> pll::find(double f_out_mhz) {

    if (!(f_out_mhz > 0.0))
        return std::nullopt;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < 400) {
        if (c >= kSearchCeiling)
            return std::nullopt;
        ++c;
    }

    for (;;) {
        double fvco = f_out_mhz * c;
        const std::uint32_t m = static_cast<std::uint32_t>(fvco / 50);
        const double ko = (fvco / 50) - m;

        fvco = ko + m;
        fvco *= kFvcoStepMhz;

        if (ko != 0.0 && (ko <= kKoBandLow || ko >= kKoBandHigh)) {

            if (fvco > kFvcoMaxMhz)
                return std::nullopt;
            if (c >= kSearchCeiling)
                return std::nullopt;
            ++c;
        } else {
            PllParams p{};
            p.c = c;
            p.m = m;

            p.k = (ko != 0.0) ? static_cast<std::uint32_t>(ko * 4294967296.0) : 1u;
            p.approximated = false;
            return p;
        }
    }
}

PllParams pll::approximate(double f_out_mhz) {

    PllParams p{};
    p.approximated = true;
    p.k = 1u;
    if (!(f_out_mhz > 0.0))
        return p;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < 400) {
        if (c >= kSearchCeiling)
            return p;
        ++c;
    }

    const double fvco = f_out_mhz * c;
    std::uint32_t m = static_cast<std::uint32_t>(fvco / 50);
    double ko = (fvco / 50) - m;

    if (ko <= kKoBandLow) {
        ko = 0;
    } else if (ko >= kKoBandHigh) {
        ++m;
        ko = 0;
    }

    p.c = c;
    p.m = m;
    p.k = (ko != 0.0) ? static_cast<std::uint32_t>(ko * 4294967296.0) : 1u;
    return p;
}

PllBlock pll::block(const PllParams& p) {

    PllBlock b{};
    b[0] = 4;
    b[1] = encode_div(p.m);
    b[2] = 3;
    b[3] = 0x10000;
    b[4] = 5;
    b[5] = encode_div(p.c);
    b[6] = 9;
    b[7] = 2;
    b[8] = 8;
    b[9] = 7;
    b[10] = 7;
    b[11] = p.k;
    return b;
}

std::optional<PllParams> pll::solve(double f_out_mhz) {
    if (!(f_out_mhz > 0.0))
        return std::nullopt;
    if (auto p = find(f_out_mhz))
        return p;
    return approximate(f_out_mhz);
}

double pll::fpix_mhz(double f_out_mhz) {

    if (!(f_out_mhz > 0.0))
        return 0.0;

    double ko = 0.0;
    std::uint32_t c = 0;
    std::uint32_t m = 0;

    if (auto found = find(f_out_mhz)) {
        c = found->c;
        m = found->m;
        double fvco = f_out_mhz * c;
        ko = (fvco / 50) - m;
    } else {
        const PllParams p = approximate(f_out_mhz);
        if (p.c == 0)
            return 0.0;
        c = p.c;
        m = p.m;
        ko = 0.0;
    }

    double fvco = ko + m;
    fvco *= kFvcoStepMhz;
    return fvco / c;
}

}
