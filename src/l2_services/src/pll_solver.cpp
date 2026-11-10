// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/pll_solver.h"
#include "svc/pll_params.h"
#include "svc/pll_payload.h"

namespace mister::svc {

namespace {

constexpr double kFvcoStepMhz = static_cast<double>(50.f);
constexpr double kFvcoMaxMhz = static_cast<double>(1500.f);
constexpr double kKoBandLow = static_cast<double>(0.05f);
constexpr double kKoBandHigh = static_cast<double>(0.95f);

static_assert(kKoBandLow != 0.05);
static_assert(kKoBandHigh != 0.95);
static_assert(kFvcoStepMhz == 50.0 && kFvcoMaxMhz == 1500.0);

constexpr std::uint32_t kSearchCeiling = 1u << 20;

}

std::uint32_t pll::encode_div(std::uint32_t div) {

    if (div & 1u) {
        return kPllAsymmetricBit | (((div / 2u) + 1u) << kPllDivHalfShift) | (div / 2u);
    }
    return ((div / 2u) << kPllDivHalfShift) | (div / 2u);
}

std::optional<PllParams> pll::find(double f_out_mhz) {

    if (!(f_out_mhz > 0.0))
        return std::nullopt;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < kFvcoFloorMhz) {
        if (c >= kSearchCeiling)
            return std::nullopt;
        ++c;
    }

    for (;;) {
        double fvco = f_out_mhz * c;
        const std::uint32_t m = static_cast<std::uint32_t>(fvco / kFvcoStepMhz);
        const double ko = (fvco / kFvcoStepMhz) - m;

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

            p.k = (ko != 0.0) ? static_cast<std::uint32_t>(ko * kFractionScale) : kPllZeroFraction;
            p.approximated = false;
            return p;
        }
    }
}

PllParams pll::approximate(double f_out_mhz) {

    PllParams p{};
    p.approximated = true;
    p.k = kPllZeroFraction;
    if (!(f_out_mhz > 0.0))
        return p;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < kFvcoFloorMhz) {
        if (c >= kSearchCeiling)
            return p;
        ++c;
    }

    const double fvco = f_out_mhz * c;
    std::uint32_t m = static_cast<std::uint32_t>(fvco / kFvcoStepMhz);
    double ko = (fvco / kFvcoStepMhz) - m;

    if (ko <= kKoBandLow) {
        ko = 0;
    } else if (ko >= kKoBandHigh) {
        ++m;
        ko = 0;
    }

    p.c = c;
    p.m = m;
    p.k = (ko != 0.0) ? static_cast<std::uint32_t>(ko * kFractionScale) : kPllZeroFraction;
    return p;
}

PllBlock pll::block(const PllParams& p) {

    PllBlock b{};
    b.writes[0] = PllWrite{PllReg::M, PllPayload{encode_div(p.m)}};
    b.writes[1] = PllWrite{PllReg::N, kPllNPayload};
    b.writes[2] = PllWrite{PllReg::C, PllPayload{encode_div(p.c)}};
    b.writes[3] = PllWrite{PllReg::ChargePump, kPllChargePumpValue};
    b.writes[4] = PllWrite{PllReg::Bandwidth, kPllBandwidthValue};
    b.writes[5] = PllWrite{PllReg::K, PllPayload{p.k}};
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
        ko = (fvco / kFvcoStepMhz) - m;
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
