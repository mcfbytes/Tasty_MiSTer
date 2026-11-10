// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/cvt.h"

#include <cmath>
#include <cstdint>

namespace mister::svc {

namespace {

constexpr double kFpixRbRetryMhz = static_cast<double>(210.f);
static_assert(kFpixRbRetryMhz == 210.0);

constexpr int kCellGranRnd = 4;

constexpr float kPercentScale = 100.0f;

constexpr float kCvtRbMinVBlankUs = 460.0f;
constexpr float kCvtMinVSyncBackPorchUs = 550.0f;
constexpr int kCvtRbHBlank = 160;
constexpr int kCvtRbHSync = 32;
constexpr int kCvtRbHBackPorch = 80;
constexpr int kCvtMinVBporch = 6;
constexpr int kCvtVFrontPorch = 3;
constexpr int kCvtRbVFrontPorch = 3;

constexpr float kCvtCPrime = 30.0f;
constexpr float kCvtMPrime = 300.0f;
constexpr float kCvtHSyncPct = 0.08f;
constexpr float kCvtMinIdealDutyPct = 20.0f;

constexpr std::uint32_t kCvtPixelRepeatWidth = 2048;
constexpr std::uint32_t kCvtFallbackWidth = 1920;
constexpr std::uint32_t kCvtFallbackLines = 1080;

constexpr int kCvtDefaultVSync = 10;

struct AspectRule {
    int arx;
    int ary;
    int vsync;

    [[nodiscard]] constexpr int width_at(int h) const noexcept {
        return ((h * arx) / (ary * kCellGranRnd)) * kCellGranRnd;
    }
};

constexpr AspectRule kCvtAspectRules[] = {
    {4, 3, 4},
    {16, 9, 5},
    {16, 10, 6},
    {5, 4, 7},
    {15, 9, 7},
};

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
