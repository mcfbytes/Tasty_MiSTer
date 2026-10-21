// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"

#include <cmath>

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

}

std::span<const PresetMode> tvmodes() noexcept {
    return std::span<const PresetMode>(kTvmodes, kNumTvmodes);
}

std::array<std::uint32_t, 26> Modeline::to_words(const PllBlock& pll) const {

    std::array<std::uint32_t, 26> w{};
    w[0] = 0;
    w[1] = hact;
    w[2] = hfp;
    w[3] = hs;
    w[4] = hbp;
    w[5] = vact;
    w[6] = vfp;
    w[7] = vs;
    w[8] = vbp;
    for (std::size_t i = 0; i < pll.size(); ++i)
        w[9 + i] = pll[i];
    w[21] = hpol ? 1u : 0u;
    w[22] = vpol ? 1u : 0u;
    w[23] = vic;
    w[24] = rb;
    w[25] = pixel_repeat ? 1u : 0u;
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
    const std::array<std::uint32_t, 26> fix = timing.to_words(pll);

    std::array<std::uint16_t, 26> out{};
    std::size_t n = 0;
    for (std::size_t i = 1; i <= 8; ++i) {
        std::uint32_t v = fix[i];
        if (i == 1) {

            if (pixel_repeat)
                v |= 1u << 15;
            if (opt.use_vrr)
                v |= 1u << 14;
        } else if (i == 3) {
            if (hpol)
                v |= 1u << 15;
        } else if (i == 7) {
            if (vpol)
                v |= 1u << 15;
        }
        out[n++] = static_cast<std::uint16_t>(v & 0xFFFFu);
    }
    for (std::size_t i = 9; i <= 20; ++i) {
        const std::uint32_t v = pll[i - 9];
        if (i & 1u) {
            std::uint32_t word = v | 0x4000u;
            if (i == 9 && opt.vsync_align)
                word |= 0x8000u;
            out[n++] = static_cast<std::uint16_t>(word & 0xFFFFu);
        } else {
            out[n++] = static_cast<std::uint16_t>(v & 0xFFFFu);
            out[n++] = static_cast<std::uint16_t>((v >> 16) & 0xFFFFu);
        }
    }
    return out;
}

namespace {

constexpr int kCellGranRnd = 4;

int determine_vsync(int w, int h) {
    const int arx[] = {4, 16, 16, 5, 15};
    const int ary[] = {3, 9, 10, 4, 9};
    const int vsync[] = {4, 5, 6, 7, 7};
    for (int ar = 0; ar < 5; ++ar) {
        const int w_calc = ((h * arx[ar]) / (ary[ar] * kCellGranRnd)) * kCellGranRnd;
        if (w_calc == w)
            return vsync[ar];
    }
    return 10;
}

Modeline cvt_int(int h_pixels, int v_lines, float refresh_rate, bool reduced_blanking) {
    const float kClockStep = 0.25f;
    const int kMinVBporch = 6;
    const int kVFrontPorch = 3;

    const int h_pixels_rnd = (h_pixels / kCellGranRnd) * kCellGranRnd;
    const int v_sync = determine_vsync(h_pixels_rnd, v_lines);

    int v_back_porch = 0;
    int h_blank = 0, h_sync = 0, h_back_porch = 0, h_front_porch = 0;
    int total_pixels = 0;
    float pixel_freq = 0.0f;

    if (reduced_blanking) {
        const int kRbVFporch = 3;
        const float kRbMinVBlank = 460.0f;

        const float h_period_est = ((1000000.0f / refresh_rate) - kRbMinVBlank)
            / static_cast<float>(v_lines);
        h_blank = 160;

        const int vbi_lines = static_cast<int>(kRbMinVBlank / h_period_est) + 1;
        const int rb_min_vbi = kRbVFporch + v_sync + kMinVBporch;
        const int act_vbi_lines = (vbi_lines < rb_min_vbi) ? rb_min_vbi : vbi_lines;
        const int total_v_lines = act_vbi_lines + v_lines;

        total_pixels = h_blank + h_pixels_rnd;
        pixel_freq = kClockStep
            * std::floor(static_cast<float>(
                             refresh_rate
                             * static_cast<float>(total_v_lines * total_pixels)
                             / 1000000.0f
                         )
                         / kClockStep);

        v_back_porch = act_vbi_lines - kVFrontPorch - v_sync;
        h_sync = 32;
        h_back_porch = 80;
        h_front_porch = h_blank - h_sync - h_back_porch;
    } else {
        const float kMinVsyncBp = 550.0f;
        const float kCPrime = 30.0f;
        const float kMPrime = 300.0f;
        const float kHSyncPer = 0.08f;

        const float h_period_est = ((1.0f / refresh_rate) - kMinVsyncBp / 1000000.0f)
            / static_cast<float>(v_lines + kVFrontPorch)
            * 1000000.0f;

        int v_sync_bp = static_cast<int>(kMinVsyncBp / h_period_est) + 1;
        if (v_sync_bp < (v_sync + kMinVBporch))
            v_sync_bp = v_sync + kMinVBporch;
        v_back_porch = v_sync_bp - v_sync;

        const float ideal_duty_cycle = kCPrime - (kMPrime * h_period_est / 1000.0f);
        if (ideal_duty_cycle < 20) {
            h_blank = (h_pixels_rnd / 4 / (2 * kCellGranRnd)) * (2 * kCellGranRnd);
        } else {
            h_blank = static_cast<int>(
                          static_cast<float>(h_pixels_rnd)
                          * ideal_duty_cycle
                          / (100.0f - ideal_duty_cycle)
                          / static_cast<float>(2 * kCellGranRnd)
                      )
                * (2 * kCellGranRnd);
        }

        total_pixels = h_pixels_rnd + h_blank;
        h_sync = static_cast<int>(
                     kHSyncPer * static_cast<float>(total_pixels) / static_cast<float>(kCellGranRnd)
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
    m.vfp = static_cast<std::uint32_t>(kVFrontPorch - 1);
    m.vs = static_cast<std::uint32_t>(v_sync);
    m.vbp = static_cast<std::uint32_t>(v_back_porch + 1);
    m.rb = reduced_blanking ? 1u : 0u;
    m.fpix_mhz = static_cast<double>(pixel_freq);

    if (h_pixels_rnd > 2048) {
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
    std::uint32_t h_pixels,
    std::uint32_t v_lines,
    float refresh_rate,
    std::uint8_t reduced_blanking,
    bool supports_pr
) {

    if (h_pixels > 2048 && !supports_pr) {
        return calculate_cvt(1920, 1080, refresh_rate, reduced_blanking, true);
    }

    Modeline m = cvt_int(
        static_cast<int>(h_pixels),
        static_cast<int>(v_lines),
        refresh_rate,
        reduced_blanking == 1
    );

    if (m.fpix_mhz > kFpixRbRetryMhz && reduced_blanking == 2) {
        m = cvt_int(static_cast<int>(h_pixels), static_cast<int>(v_lines), refresh_rate, true);
    }
    return m;
}

}
