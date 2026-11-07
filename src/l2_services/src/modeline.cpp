// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"

#include <cmath>
#include <iterator>

#include "svc/modeline_item.h"
#include "video_service_detail.h"

namespace mister::svc {

namespace {

constexpr PresetMode kTvmodes[kNumTvmodes] = {
    {{640, 30, 60, 70, 240, 4, 4, 14}, 12.587, 0, 0},
    {{640, 16, 96, 48, 480, 8, 4, 33}, 25.175, 0, 0},
    {{640, 30, 60, 70, 288, 6, 4, 14}, 12.587, 0, 0},
    {{640, 16, 96, 48, 576, 2, 4, 42}, 25.175, 0, 0},
};

constexpr std::uint32_t kFbDvLbrd = 3;
constexpr std::uint32_t kFbDvRbrd = 6;
constexpr std::uint32_t kFbDvUbrd = 2;
constexpr std::uint32_t kFbDvBbrd = 2;

constexpr std::uint32_t kTimingFlagBit = 1u << 15;
constexpr std::uint32_t kWireVrrBit = 1u << 14;
constexpr std::uint32_t kWirePllAddressSelector = 0x4000u;
constexpr std::uint32_t kWireVsyncAlign = 0x8000u;

}

std::span<const PresetMode> tvmodes() noexcept {
    return std::span<const PresetMode>(kTvmodes, kNumTvmodes);
}

std::array<std::uint32_t, 26> Modeline::to_words(const PllBlock& pll) const {

    std::array<std::uint32_t, kModelineItems> w{};
    w[item(ModelineItem::Mode)] = 0;
    w[item(ModelineItem::Hact)] = hact;
    w[item(ModelineItem::Hfp)] = hfp;
    w[item(ModelineItem::Hs)] = hs;
    w[item(ModelineItem::Hbp)] = hbp;
    w[item(ModelineItem::Vact)] = vact;
    w[item(ModelineItem::Vfp)] = vfp;
    w[item(ModelineItem::Vs)] = vs;
    w[item(ModelineItem::Vbp)] = vbp;
    for (std::size_t i = 0; i < pll.size(); ++i)
        w[item(ModelineItem::PllFirst) + i] = pll[i];
    w[item(ModelineItem::Hpol)] = hpol ? 1u : 0u;
    w[item(ModelineItem::Vpol)] = vpol ? 1u : 0u;
    w[item(ModelineItem::Vic)] = vic;
    w[item(ModelineItem::Rb)] = rb;
    w[item(ModelineItem::PixelRepeat)] = pixel_repeat ? 1u : 0u;
    return w;
}

Modeline Modeline::direct_video_fix() const {

    Modeline f = *this;
    f.hfp = kFbDvRbrd;
    f.hbp = kFbDvLbrd;
    f.hact = hact + (hfp - f.hfp) + (hbp - f.hbp);
    f.vfp = kFbDvBbrd;
    f.vbp = kFbDvUbrd;
    f.vact = vact + (vfp - f.vfp) + (vbp - f.vbp);
    return f;
}

std::array<std::uint16_t, 26> Modeline::to_wire(const PllBlock& pll, const WireOptions& opt) const {

    const Modeline timing = opt.direct_video ? direct_video_fix() : *this;
    const std::array<std::uint32_t, kModelineItems> fix = timing.to_words(pll);

    std::array<std::uint16_t, 26> out{};
    std::size_t n = 0;
    for (std::size_t i = item(ModelineItem::Hact); i <= item(ModelineItem::Vbp); ++i) {
        std::uint32_t v = fix[i];
        if (i == item(ModelineItem::Hact)) {

            if (pixel_repeat)
                v |= kTimingFlagBit;
            if (opt.use_vrr)
                v |= kWireVrrBit;
        } else if (i == item(ModelineItem::Hs)) {
            if (hpol)
                v |= kTimingFlagBit;
        } else if (i == item(ModelineItem::Vs)) {
            if (vpol)
                v |= kTimingFlagBit;
        }
        out[n++] = static_cast<std::uint16_t>(v);
    }
    for (std::size_t i = item(ModelineItem::PllFirst); i <= item(ModelineItem::PllLast); ++i) {
        const std::uint32_t v = pll[i - item(ModelineItem::PllFirst)];
        if (i & 1u) {
            std::uint32_t word = v | kWirePllAddressSelector;
            if (i == item(ModelineItem::PllFirst) && opt.vsync_align)
                word |= kWireVsyncAlign;
            out[n++] = static_cast<std::uint16_t>(word);
        } else {
            out[n++] = static_cast<std::uint16_t>(v);
            out[n++] = static_cast<std::uint16_t>((v >> 16));
        }
    }
    return out;
}

namespace {

int determine_vsync(int w, int h) {
    for (const AspectRule& r : kCvtAspectRules) {
        if (r.width_at(h) == w)
            return r.vsync;
    }
    return kCvtDefaultVSync;
}

Modeline cvt_int(Resolution active, float refresh_rate, bool reduced_blanking) {
    constexpr float kClockStep = 0.25f;
    constexpr float kUsPerSecond = 1'000'000.0f;

    const int h_pixels = static_cast<int>(active.width);
    const int v_lines = static_cast<int>(active.height);
    const int h_pixels_rnd = (h_pixels / kCellGranRnd) * kCellGranRnd;
    const int v_sync = determine_vsync(h_pixels_rnd, v_lines);

    int v_back_porch = 0;
    int h_blank = 0, h_sync = 0, h_back_porch = 0, h_front_porch = 0;
    int total_pixels = 0;
    float pixel_freq = 0.0f;

    if (reduced_blanking) {
        const float h_period_est = ((kUsPerSecond / refresh_rate) - kCvtRbMinVBlankUs)
            / static_cast<float>(v_lines);
        h_blank = kCvtRbHBlank;

        const int vbi_lines = static_cast<int>(kCvtRbMinVBlankUs / h_period_est) + 1;
        const int rb_min_vbi = kCvtRbVFrontPorch + v_sync + kCvtMinVBporch;
        const int act_vbi_lines = (vbi_lines < rb_min_vbi) ? rb_min_vbi : vbi_lines;
        const int total_v_lines = act_vbi_lines + v_lines;

        total_pixels = h_blank + h_pixels_rnd;
        pixel_freq = kClockStep
            * std::floor(static_cast<float>(
                             refresh_rate
                             * static_cast<float>(total_v_lines * total_pixels)
                             / kUsPerSecond
                         )
                         / kClockStep);

        v_back_porch = act_vbi_lines - kCvtVFrontPorch - v_sync;
        h_sync = kCvtRbHSync;
        h_back_porch = kCvtRbHBackPorch;
        h_front_porch = h_blank - h_sync - h_back_porch;
    } else {
        const float h_period_est = ((1.0f / refresh_rate) - kCvtMinVSyncBackPorchUs / kUsPerSecond)
            / static_cast<float>(v_lines + kCvtVFrontPorch)
            * kUsPerSecond;

        int v_sync_bp = static_cast<int>(kCvtMinVSyncBackPorchUs / h_period_est) + 1;
        if (v_sync_bp < (v_sync + kCvtMinVBporch))
            v_sync_bp = v_sync + kCvtMinVBporch;
        v_back_porch = v_sync_bp - v_sync;

        const float ideal_duty_cycle = kCvtCPrime - (kCvtMPrime * h_period_est / 1000.0f);
        if (ideal_duty_cycle < kCvtMinIdealDutyPct) {
            h_blank = (h_pixels_rnd / 4 / (2 * kCellGranRnd)) * (2 * kCellGranRnd);
        } else {
            h_blank = static_cast<int>(
                          static_cast<float>(h_pixels_rnd)
                          * ideal_duty_cycle
                          / (kPercentScale - ideal_duty_cycle)
                          / static_cast<float>(2 * kCellGranRnd)
                      )
                * (2 * kCellGranRnd);
        }

        total_pixels = h_pixels_rnd + h_blank;
        h_sync = static_cast<int>(
                     kCvtHSyncPct
                     * static_cast<float>(total_pixels)
                     / static_cast<float>(kCellGranRnd)
                 )
            * kCellGranRnd;
        h_back_porch = h_blank / 2;
        h_front_porch = h_blank - h_sync - h_back_porch;
        pixel_freq = kClockStep
            * std::floor(static_cast<float>(total_pixels) / h_period_est / kClockStep);
    }

    Modeline m{};
    m.hact = static_cast<std::uint32_t>(h_pixels_rnd);
    m.hfp = static_cast<std::uint32_t>(h_front_porch);
    m.hs = static_cast<std::uint32_t>(h_sync);
    m.hbp = static_cast<std::uint32_t>(h_back_porch);
    m.vact = static_cast<std::uint32_t>(v_lines);
    m.vfp = static_cast<std::uint32_t>(kCvtVFrontPorch - 1);
    m.vs = static_cast<std::uint32_t>(v_sync);
    m.vbp = static_cast<std::uint32_t>(v_back_porch + 1);
    m.rb = rb_byte(reduced_blanking ? ReducedBlanking::On : ReducedBlanking::Off);
    m.fpix_mhz = static_cast<double>(pixel_freq);

    if (static_cast<std::uint32_t>(h_pixels_rnd) > kCvtPixelRepeatWidth) {
        m.pixel_repeat = true;
        m.hact /= 2;
        m.hbp /= 2;
        m.hfp /= 2;
        m.hs /= 2;
        m.fpix_mhz /= 2.0;
    } else {
        m.pixel_repeat = false;
    }
    return m;
}

}

Modeline calculate_cvt(
    Resolution active,
    float refresh_rate,
    ReducedBlanking reduced_blanking,
    bool supports_pr
) {

    if (active.width > kCvtPixelRepeatWidth && !supports_pr) {
        return calculate_cvt(
            Resolution{kCvtFallbackWidth, kCvtFallbackLines},
            refresh_rate,
            reduced_blanking,
            true
        );
    }

    Modeline m = cvt_int(active, refresh_rate, reduced_blanking == ReducedBlanking::On);

    if (m.fpix_mhz > kFpixRbRetryMhz && reduced_blanking == ReducedBlanking::OnIfClockHigh) {
        m = cvt_int(active, refresh_rate, true);
    }
    return m;
}

}
