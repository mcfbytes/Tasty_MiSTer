// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"
#include "hal/selected.h"

#include "os/clock.h"
#include "os/delay.h"
#include "hal/hdmi_int.h"
#include "svc/adv7513_init_options.h"
#include "svc/adv7513_reg_map.h"
#include "svc/adv7513_reg_write.h"
#include "svc/filter_resend.h"
#include "svc/geometry_gate.h"
#include "svc/pll_params.h"
#include "svc/scaling_words.h"

#include <bit>
#include <cctype>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstring>

namespace mister::svc {

namespace {

bool parse_u32_base0(std::string_view s, std::uint32_t& out) {
    if (s.empty()) return false;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s.remove_prefix(2);
    } else if (s.size() > 1 && s[0] == '0') {
        base = 8;
        s.remove_prefix(1);
    }
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out, base);
    return ec == std::errc{} && ptr == last;
}

bool parse_double_all(std::string_view s, double& out) {
    if (s.empty()) return false;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc{} && ptr == last;
}

std::int64_t strtoull_prefix(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        ++i;
    }
    unsigned base = 10;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (i < s.size() && s[i] == '0') {
        base = 8;
        ++i;
    }
    std::uint64_t v = 0;
    for (; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<unsigned>(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<unsigned>(c - 'A') + 10u;
        else
            break;
        if (digit >= base) break;
        v = v * base + digit;
    }
    if (neg) v = ~v + 1u;
    return static_cast<std::int64_t>(v);
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

constexpr std::size_t kMaxTokens = 32;

std::size_t tokenize_commas(std::string_view s, std::array<std::string_view, kMaxTokens>& out) {
    std::size_t n = 0;
    std::size_t i = 0;
    while (i < s.size() && n < kMaxTokens) {
        while (i < s.size() && s[i] == ',')
            ++i;
        if (i >= s.size()) break;
        const std::size_t start = i;
        while (i < s.size() && s[i] != ',')
            ++i;
        out[n++] = s.substr(start, i - start);
    }
    return n;
}

std::unexpected<Error> err(Errc c, std::uint16_t site, std::uint32_t detail = 0) {
    return std::unexpected(Error{c, site, detail});
}

constexpr PresetMode kVmodes[kNumVmodes] = {
    {{1280, 110, 40, 220, 720, 5, 5, 20}, 74.25, 4, 0},
    {{1024, 24, 136, 160, 768, 3, 6, 29}, 65.0, 0, 0},
    {{720, 16, 62, 60, 480, 9, 6, 30}, 27.0, 3, 0},
    {{720, 12, 64, 68, 576, 5, 5, 39}, 27.0, 18, 0},
    {{1280, 48, 112, 248, 1024, 1, 3, 38}, 108.0, 0, 0},
    {{800, 40, 128, 88, 600, 1, 4, 23}, 40.0, 0, 0},
    {{640, 16, 96, 48, 480, 10, 2, 33}, 25.175, 1, 0},
    {{1280, 440, 40, 220, 720, 5, 5, 20}, 74.25, 19, 0},
    {{1920, 88, 44, 148, 1080, 4, 5, 36}, 148.5, 16, 0},
    {{1920, 528, 44, 148, 1080, 4, 5, 36}, 148.5, 31, 0},
    {{1366, 70, 143, 213, 768, 3, 3, 24}, 85.5, 0, 0},
    {{1024, 40, 104, 144, 600, 1, 3, 18}, 48.96, 0, 0},
    {{1920, 48, 32, 80, 1440, 2, 4, 38}, 185.203, 0, 0},
    {{2048, 48, 32, 80, 1536, 2, 4, 38}, 209.318, 0, 0},

    {{1280, 24, 16, 40, 1440, 3, 5, 33}, 120.75, 0, 1},
};

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

constexpr std::uint8_t kCecRegRxHeader[3] = {0x15, 0x27, 0x38};
constexpr std::uint8_t kCecRegRxLength[3] = {0x25, 0x37, 0x48};
constexpr std::uint8_t kCecRegRxStatus = 0x26;
constexpr std::uint8_t kCecRegRxReady = 0x49;
constexpr std::uint8_t kCecRegRxBuffers = 0x4A;
constexpr std::uint8_t kMainRegInt1Status = 0x97;
constexpr std::uint8_t kCecIntRxRdyMask = 0x07;

constexpr std::uint8_t kEdidMagic[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

constexpr double kFvcoStepMhz = static_cast<double>(50.f);
constexpr double kFvcoMaxMhz = static_cast<double>(1500.f);
constexpr double kKoBandLow = static_cast<double>(0.05f);
constexpr double kKoBandHigh = static_cast<double>(0.95f);
constexpr double kFpixRbRetryMhz = static_cast<double>(210.f);
constexpr double kFpixMinMhz = static_cast<double>(2.f);
constexpr double kFpixMaxMhz = static_cast<double>(300.f);

static_assert(kKoBandLow != 0.05);
static_assert(kKoBandHigh != 0.95);
static_assert(kFvcoStepMhz == 50.0 && kFvcoMaxMhz == 1500.0 && kFpixRbRetryMhz == 210.0 &&
              kFpixMinMhz == 2.0 && kFpixMaxMhz == 300.0);

constexpr std::uint32_t kSearchCeiling = 1u << 20;

}  // namespace

std::uint32_t pll::encode_div(std::uint32_t div) {

    if (div & 1u) {
        return 0x20000u | (((div / 2u) + 1u) << 8) | (div / 2u);
    }
    return ((div / 2u) << 8) | (div / 2u);
}

std::optional<PllParams> pll::find(double f_out_mhz) {

    if (!(f_out_mhz > 0.0)) return std::nullopt;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < 400) {
        if (c >= kSearchCeiling) return std::nullopt;
        ++c;
    }

    for (;;) {
        double fvco = f_out_mhz * c;
        const std::uint32_t m = static_cast<std::uint32_t>(fvco / 50);
        const double ko = (fvco / 50) - m;

        fvco = ko + m;
        fvco *= kFvcoStepMhz;

        if (ko != 0.0 && (ko <= kKoBandLow || ko >= kKoBandHigh)) {

            if (fvco > kFvcoMaxMhz) return std::nullopt;
            if (c >= kSearchCeiling) return std::nullopt;
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
    if (!(f_out_mhz > 0.0)) return p;

    std::uint32_t c = 1;
    while ((f_out_mhz * c) < 400) {
        if (c >= kSearchCeiling) return p;
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
    if (!(f_out_mhz > 0.0)) return std::nullopt;
    if (auto p = find(f_out_mhz)) return p;
    return approximate(f_out_mhz);
}

double pll::fpix_mhz(double f_out_mhz) {

    if (!(f_out_mhz > 0.0)) return 0.0;

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
        if (p.c == 0) return 0.0;
        c = p.c;
        m = p.m;
        ko = 0.0;
    }

    double fvco = ko + m;
    fvco *= kFvcoStepMhz;
    return fvco / c;
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

            if (pixel_repeat) v |= 1u << 15;
            if (opt.use_vrr) v |= 1u << 14;
        } else if (i == 3) {
            if (hpol) v |= 1u << 15;
        } else if (i == 7) {
            if (vpol) v |= 1u << 15;
        }
        out[n++] = static_cast<std::uint16_t>(v & 0xFFFFu);
    }
    for (std::size_t i = 9; i <= 20; ++i) {
        const std::uint32_t v = pll[i - 9];
        if (i & 1u) {
            std::uint32_t word = v | 0x4000u;
            if (i == 9 && opt.vsync_align) word |= 0x8000u;
            out[n++] = static_cast<std::uint16_t>(word & 0xFFFFu);
        } else {
            out[n++] = static_cast<std::uint16_t>(v & 0xFFFFu);
            out[n++] = static_cast<std::uint16_t>((v >> 16) & 0xFFFFu);
        }
    }
    return out;
}

std::span<const PresetMode> vmodes() noexcept {
    return std::span<const PresetMode>(kVmodes, kNumVmodes);
}

std::span<const PresetMode> tvmodes() noexcept {
    return std::span<const PresetMode>(kTvmodes, kNumTvmodes);
}

namespace {

constexpr int kCellGranRnd = 4;

int determine_vsync(int w, int h) {
    const int arx[] = {4, 16, 16, 5, 15};
    const int ary[] = {3, 9, 10, 4, 9};
    const int vsync[] = {4, 5, 6, 7, 7};
    for (int ar = 0; ar < 5; ++ar) {
        const int w_calc = ((h * arx[ar]) / (ary[ar] * kCellGranRnd)) * kCellGranRnd;
        if (w_calc == w) return vsync[ar];
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

        const float h_period_est =
            ((1000000.0f / refresh_rate) - kRbMinVBlank) / static_cast<float>(v_lines);
        h_blank = 160;

        const int vbi_lines = static_cast<int>(kRbMinVBlank / h_period_est) + 1;
        const int rb_min_vbi = kRbVFporch + v_sync + kMinVBporch;
        const int act_vbi_lines = (vbi_lines < rb_min_vbi) ? rb_min_vbi : vbi_lines;
        const int total_v_lines = act_vbi_lines + v_lines;

        total_pixels = h_blank + h_pixels_rnd;
        pixel_freq = kClockStep *
                     std::floor(static_cast<float>(
                                    refresh_rate *
                                    static_cast<float>(total_v_lines * total_pixels) / 1000000.0f) /
                                kClockStep);

        v_back_porch = act_vbi_lines - kVFrontPorch - v_sync;
        h_sync = 32;
        h_back_porch = 80;
        h_front_porch = h_blank - h_sync - h_back_porch;
    } else {
        const float kMinVsyncBp = 550.0f;
        const float kCPrime = 30.0f;
        const float kMPrime = 300.0f;
        const float kHSyncPer = 0.08f;

        const float h_period_est = ((1.0f / refresh_rate) - kMinVsyncBp / 1000000.0f) /
                                   static_cast<float>(v_lines + kVFrontPorch) * 1000000.0f;

        int v_sync_bp = static_cast<int>(kMinVsyncBp / h_period_est) + 1;
        if (v_sync_bp < (v_sync + kMinVBporch)) v_sync_bp = v_sync + kMinVBporch;
        v_back_porch = v_sync_bp - v_sync;

        const float ideal_duty_cycle = kCPrime - (kMPrime * h_period_est / 1000.0f);
        if (ideal_duty_cycle < 20) {
            h_blank = (h_pixels_rnd / 4 / (2 * kCellGranRnd)) * (2 * kCellGranRnd);
        } else {
            h_blank = static_cast<int>(static_cast<float>(h_pixels_rnd) * ideal_duty_cycle /
                                       (100.0f - ideal_duty_cycle) /
                                       static_cast<float>(2 * kCellGranRnd)) *
                      (2 * kCellGranRnd);
        }

        total_pixels = h_pixels_rnd + h_blank;
        h_sync = static_cast<int>(kHSyncPer * static_cast<float>(total_pixels) /
                                  static_cast<float>(kCellGranRnd)) *
                 kCellGranRnd;
        h_back_porch = h_blank / 2;
        h_front_porch = h_blank - h_sync - h_back_porch;
        pixel_freq =
            kClockStep * std::floor(static_cast<float>(total_pixels) / h_period_est / kClockStep);
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

}  // namespace

Modeline calculate_cvt(std::uint32_t h_pixels, std::uint32_t v_lines, float refresh_rate,
                       std::uint8_t reduced_blanking, bool supports_pr) {

    if (h_pixels > 2048 && !supports_pr) {
        return calculate_cvt(1920, 1080, refresh_rate, reduced_blanking, true);
    }

    Modeline m = cvt_int(static_cast<int>(h_pixels), static_cast<int>(v_lines), refresh_rate,
                         reduced_blanking == 1);

    if (m.fpix_mhz > kFpixRbRetryMhz && reduced_blanking == 2) {
        m = cvt_int(static_cast<int>(h_pixels), static_cast<int>(v_lines), refresh_rate, true);
    }
    return m;
}

std::string_view YcMode::key_view() const noexcept {
    std::size_t n = 0;
    while (n < key.size() && key[n] != '\0')
        ++n;
    return std::string_view(key.data(), n);
}

std::vector<YcMode> parse_yc(std::string_view text, std::string_view core_name) {
    std::vector<YcMode> out;
    out.reserve(kYcMaxEntries);

    std::size_t pos = 0;
    while (out.size() < kYcMaxEntries && pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line =
            (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);

        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        const bool match =
            line.size() >= core_name.size() && iequals(line.substr(0, core_name.size()), core_name);
        if (match) {
            const std::size_t eq = line.find('=');

            if (eq != std::string_view::npos) {
                std::size_t v = eq + 1;
                while (v < line.size() && (line[v] == '=' || line[v] == ' ' || line[v] == '\t')) {
                    ++v;
                }
                YcMode e{};
                const std::string_view k = line.substr(0, eq);
                const std::size_t n = (k.size() < e.key.size() - 1) ? k.size() : e.key.size() - 1;
                std::memcpy(e.key.data(), k.data(), n);
                e.phase_inc = strtoull_prefix(line.substr(v));

                if (e.phase_inc != 0) out.push_back(e);
            }
        }

        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return out;
}

std::optional<std::int64_t> yc_lookup(std::span<const YcMode> table, std::string_view key,
                                      std::string_view key_expanded) {

    for (const YcMode& e : table) {
        const std::string_view k = e.key_view();
        if (iequals(k, key) || iequals(k, key_expanded)) return e.phase_inc;
    }
    return std::nullopt;
}

namespace adv7513 {

std::array<RegWrite, kInitTableSize> init_table(const InitOptions& o) {

    const std::uint8_t int0 = o.has_hdmi_int ? 0xC0 : 0x00;

    const std::uint8_t reg56 = 0x08;

    const std::uint8_t reg57 = static_cast<std::uint8_t>((o.hdmi_game_mode ? 0x80u : 0x00u) |
                                                         ((o.ypbpr || o.hdmi_limited) ? 0x04u
                                                          : o.hdr_mode                ? 0x68u
                                                                                      : 0x08u));

    return std::array<RegWrite, kInitTableSize>{{
        {0x98, 0x03},
        {0xD6, 0xC0},
        {0x41, 0x10},
        {0x9A, 0x70},
        {0x9C, 0x30},
        {0x9D, 0x61},
        {0xA2, 0xA4},
        {0xA3, 0xA4},
        {0xE0, 0xD0},
        {0x35, 0x40},
        {0x36, 0xD9},
        {0x37, 0x0A},
        {0x38, 0x00},
        {0x39, 0x2D},
        {0x3A, 0x00},
        {0x16, 0x38},
        {0x17, 0x62},
        {0x3B, 0x80},
        {0x3C, 0x00},
        {0x48, 0x08},
        {0x49, 0xA8},
        {0x40, 0x00},
        {0x4A, 0x80},
        {0x4C, 0x00},
        {0x55, static_cast<std::uint8_t>(o.hdmi_game_mode ? 0x12 : 0x10)},
        {0x56, reg56},
        {0x57, reg57},
        {0x59, static_cast<std::uint8_t>(o.hdmi_game_mode ? 0x30 : 0x00)},
        {0x73, 0x01},
        {0x96, 0xFF},
        {0x94, int0},
        {0xC9, 0x00},
        {0x99, 0x02},
        {0x9B, 0x18},
        {0x9F, 0x00},
        {0xA1, 0x00},
        {0xA4, 0x08},
        {0xA5, 0x04},
        {0xA6, 0x00},
        {0xA7, 0x00},
        {0xA8, 0x00},
        {0xA9, 0x00},
        {0xAA, 0x00},
        {0xAB, 0x40},
        {0xB9, 0x00},
        {0xBA, 0x60},
        {0xBB, 0x00},
        {0xDE, 0x9C},

        {0xE2, 0x01},
        {0xE4, 0x60},
        {0xFA, 0x7D},
    }};
}

std::array<RegWrite, kAudioTableSize> audio_table(const InitOptions& o) {

    return std::array<RegWrite, kAudioTableSize>{{
        {0xAF, static_cast<std::uint8_t>(0x04u | (o.dvi_mode ? 0x00u : 0x02u))},
        {0x0A, 0x00},
        {0x0B, 0x0E},
        {0x0C, 0x04},
        {0x0D, 0x10},
        {0x14, 0x02},
        {0x15, static_cast<std::uint8_t>((o.hdmi_audio_96k ? 0x80u : 0x00u) | 0x20u)},
        {0x01, 0x00},
        {0x02, static_cast<std::uint8_t>(o.hdmi_audio_96k ? 0x30 : 0x18)},
        {0x03, 0x00},
        {0x07, 0x01},
        {0x08, 0x22},
        {0x09, 0x0A},
    }};
}

std::array<RegWrite, 3> mode_table(const Modeline& m, bool direct_video_menu) {

    std::uint8_t pr_flags;
    if (direct_video_menu) {
        pr_flags = 0x00;
    } else if (m.pixel_repeat) {
        pr_flags = 0x48;
    } else {
        pr_flags = 0x40;
    }

    std::uint8_t sync_invert = 0;
    if (!m.hpol) sync_invert |= 1u << 5;
    if (!m.vpol) sync_invert |= 1u << 6;

    return std::array<RegWrite, 3>{{
        {0x17, static_cast<std::uint8_t>(0x02u | sync_invert)},
        {0x3B, pr_flags},
        {0x3C, m.vic},
    }};
}

RegWrite power(bool on) { return RegWrite{kRegPower, static_cast<std::uint8_t>(on ? 0x10 : 0x50)}; }

}  // namespace adv7513

namespace {

std::uint32_t now_ms(const adv7513::Io& io) {
    return static_cast<std::uint32_t>(io.clock->now().count() / 1'000'000);
}

bool edid_header_valid(std::span<const std::byte> buf) {
    if (buf.size() < 8) return false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::to_integer<std::uint8_t>(buf[i]) != kEdidMagic[i]) return false;
    }
    return true;
}

}  // namespace

Ex<bool> EdidStore::read_segment(std::uint8_t segment, std::span<std::byte> out) {

    if (auto r = io_.main->write(adv7513::kRegIntStatus, 4); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io_.main->write(adv7513::kRegEdidSegment, segment); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    io_.delay->sleep_for(std::chrono::microseconds{kTriggerSettleUs});
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x13); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    const std::uint32_t start = now_ms(io_);
    for (;;) {
        auto status = io_.main->read(adv7513::kRegIntStatus);

        if (status && (*status & 4u)) {
            if (auto r = io_.main->write(adv7513::kRegIntStatus, 4); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }

            for (std::uint16_t i = 0; i < 256; ++i) {
                auto v = io_.edid->read(static_cast<std::uint8_t>(i));
                out[i] = v ? static_cast<std::byte>(*v) : std::byte{0};
            }

            if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }
            return true;
        }
        if ((now_ms(io_) - start) >= kReadyTimeoutMs) break;
        io_.delay->sleep_for(std::chrono::microseconds{kReadyPollUs});
    }

    ++ready_timeouts_;
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return false;
}

Ex<void> EdidStore::refresh() {

    if (!io_.main || !io_.edid || !io_.delay || !io_.clock) {
        return err(Errc::negotiation, ERR_SITE());
    }

    std::array<std::byte, 256 * kMaxSegments> buf{};
    bool ddc_responded = false;
    bool ok = false;

    for (int k = 0; k < kMaxRetries; ++k) {

        auto hpd = io_.main->read(adv7513::kRegStatus);
        if (!hpd) return err(hpd.error().code, ERR_SITE(), hpd.error().detail);
        if ((*hpd & 0x60u) != 0x60u) return {};

        auto got = read_segment(0, std::span<std::byte>(buf.data(), 256));
        if (!got) return err(got.error().code, ERR_SITE(), got.error().detail);
        if (*got) ddc_responded = true;
        if (edid_header_valid(std::span<const std::byte>(buf.data(), 256))) {
            ok = true;
            break;
        }

        if (!*got) break;
        io_.delay->sleep_for(std::chrono::microseconds{kRetryDelayUs});
    }

    if (!ok) {

        (void)ddc_responded;
        return {};
    }

    std::size_t blocks = (2u + std::to_integer<std::uint8_t>(buf[126])) / 2u;
    if (blocks > kMaxSegments) blocks = kMaxSegments;
    if (blocks < 1) blocks = 1;
    for (std::size_t i = 1; i < blocks; ++i) {
        auto got = read_segment(static_cast<std::uint8_t>(i),
                                std::span<std::byte>(buf.data() + i * 256, 256));
        if (!got) return err(got.error().code, ERR_SITE(), got.error().detail);
    }

    full_ = buf;
    full_len_ = blocks * 256;
    std::memcpy(edid_.bytes.data(), buf.data(), edid_.bytes.size());
    edid_.valid = true;

    ++gen_.v;
    return {};
}

std::span<const std::byte> EdidStore::full() const noexcept {
    return std::span<const std::byte>(full_.data(), full_len_);
}

Ex<void> CecStateMachine::poll_deadline() {
    last_.reset();

    if (state_ == CecState::Disabled || state_ == CecState::AwaitingInit) {
        return {};
    }
    if (!io_.cec || !io_.main || !io_.hdmi) {
        return err(Errc::negotiation, ERR_SITE());
    }

    if (state_ != CecState::Idle) return {};

    if (!io_.hdmi->hdmi_int_asserted()) return {};

    auto ready = io_.cec->read(kCecRegRxReady);
    if (!ready) return err(ready.error().code, ERR_SITE(), ready.error().detail);
    const std::uint8_t rx_bits = static_cast<std::uint8_t>(*ready & kCecIntRxRdyMask);
    if (rx_bits == 0) {

        if (auto r = io_.main->write(kMainRegInt1Status, kCecIntRxRdyMask); !r) {
            return err(r.error().code, ERR_SITE(), r.error().detail);
        }
        return {};
    }

    const std::uint32_t depth =
        static_cast<std::uint32_t>(std::popcount(static_cast<unsigned>(rx_bits)));
    if (depth > max_rx_depth_) max_rx_depth_ = depth;

    auto order = io_.cec->read(kCecRegRxStatus);
    if (!order) return err(order.error().code, ERR_SITE(), order.error().detail);

    int selected = -1;
    int oldest = 4;
    for (int i = 0; i < kRxBuffers; ++i) {
        if (!(rx_bits & (1u << i))) continue;
        const int rank = static_cast<int>((static_cast<unsigned>(*order) >> (i * 2)) & 0x03u);
        if (rank > 0 && rank < oldest) {
            oldest = rank;
            selected = i;
        }
    }
    if (selected < 0) {
        for (int i = 0; i < kRxBuffers; ++i) {
            if (rx_bits & (1u << i)) {
                selected = i;
                break;
            }
        }
    }
    if (selected < 0) return {};

    const auto idx = static_cast<std::size_t>(selected);

    auto len_raw = io_.cec->read(kCecRegRxLength[idx]);
    if (!len_raw) return err(len_raw.error().code, ERR_SITE(), len_raw.error().detail);
    const std::uint8_t length = static_cast<std::uint8_t>(*len_raw & 0x1Fu);
    const std::uint8_t hdr_reg = kCecRegRxHeader[idx];

    if (length >= 1 && length <= 16) {
        CecMessage msg{};
        msg.length = length;
        auto h = io_.cec->read(hdr_reg);
        if (!h) return err(h.error().code, ERR_SITE(), h.error().detail);
        msg.header = *h;
        if (length > 1) {
            auto op = io_.cec->read(static_cast<std::uint8_t>(hdr_reg + 1));
            if (!op) return err(op.error().code, ERR_SITE(), op.error().detail);
            msg.opcode = *op;
        }
        const std::uint8_t payload = static_cast<std::uint8_t>(length > 2 ? length - 2 : 0);
        for (std::uint8_t i = 0; i < payload; ++i) {
            auto d = io_.cec->read(static_cast<std::uint8_t>(hdr_reg + 2 + i));
            if (!d) return err(d.error().code, ERR_SITE(), d.error().detail);
            msg.data[i] = *d;
        }
        last_ = msg;
    } else {
        ++rx_rejected_;
    }

    const std::uint8_t bit = static_cast<std::uint8_t>(1u << selected);
    if (auto r = io_.cec->write(kCecRegRxBuffers, static_cast<std::uint8_t>(0x08u | bit)); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (io_.delay) {
        io_.delay->sleep_for(std::chrono::microseconds{kReleasePulseUs});
    }
    if (auto r = io_.cec->write(kCecRegRxBuffers, 0x08); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    ++rx_released_;

    if (auto r = io_.main->write(kMainRegInt1Status, bit); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return {};
}

VideoService::VideoService(Wiring w) noexcept
    : sink_(w.sink ? &*w.sink : nullptr), sampler_(w.sampler ? &*w.sampler : nullptr) {}

VideoService VideoService::create(Wiring w) { return VideoService(w); }
VideoService VideoService::create() { return VideoService(Wiring{}); }

Ex<VideoService::ParsedMode> VideoService::parse_mode(std::string_view spec) const {
    if (spec.empty()) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    std::array<std::string_view, kMaxTokens> tokens{};
    const std::size_t token_cnt = tokenize_commas(spec, tokens);
    if (token_cnt == 0) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }

    std::array<std::uint32_t, kMaxTokens> val{};
    double valf = 0.0;
    std::size_t cnt = 0;
    for (; cnt < token_cnt; ++cnt) {
        if (!parse_u32_base0(tokens[cnt], val[cnt])) break;
    }

    if (cnt == 2 && token_cnt > 2) {
        if (parse_double_all(tokens[cnt], valf)) ++cnt;
    }

    ParsedMode out{};
    Modeline& m = out.mode;
    m.rb = 1;

    for (std::size_t i = cnt; i < token_cnt; ++i) {
        const std::string_view flag = tokens[i];
        if (iequals(flag, "+vsync"))
            m.vpol = true;
        else if (iequals(flag, "-vsync"))
            m.vpol = false;
        else if (iequals(flag, "+hsync"))
            m.hpol = true;
        else if (iequals(flag, "-hsync"))
            m.hpol = false;
        else if (iequals(flag, "cvt"))
            m.rb = 0;
        else if (iequals(flag, "cvtrb"))
            m.rb = 1;
        else if (iequals(flag, "pr"))
            m.pixel_repeat = true;
        else {
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)});
        }
    }

    if (cnt == 1) {

        out.how = ModeRequest::PresetIndex;
        out.preset_index = val[0];
        return out;
    }

    if (cnt == 3) {

        const double rate = (valf != 0.0) ? valf : static_cast<double>(val[2]);

        const bool hpol = m.hpol;
        const bool vpol = m.vpol;
        out.mode = calculate_cvt(val[0], val[1], static_cast<float>(rate), m.rb, supports_pr_);
        out.mode.hpol = hpol;
        out.mode.vpol = vpol;
        out.how = ModeRequest::Triple;
        return out;
    }

    if (cnt >= 21) {

        m.hact = val[1];
        m.hfp = val[2];
        m.hs = val[3];
        m.hbp = val[4];
        m.vact = val[5];
        m.vfp = val[6];
        m.vs = val[7];
        m.vbp = val[8];
        PllBlock pll{};
        for (std::size_t i = 0; i < pll.size(); ++i)
            pll[i] = val[9 + i];
        out.raw_pll = pll;
        if (cnt > 21) m.hpol = (val[21] != 0);
        if (cnt > 22) m.vpol = (val[22] != 0);
        if (cnt > 23) m.vic = static_cast<std::uint8_t>(val[23] & 0xFFu);
        if (cnt > 24) m.rb = static_cast<std::uint8_t>(val[24] & 0xFFu);
        if (cnt > 25) m.pixel_repeat = (val[25] != 0);

        out.how = ModeRequest::RawModeline;
        return out;
    }

    if (cnt == 9 || cnt == 11) {
        m.hact = val[0];
        m.hfp = val[1];
        m.hs = val[2];
        m.hbp = val[3];
        m.vact = val[4];
        m.vfp = val[5];
        m.vs = val[6];
        m.vbp = val[7];

        m.fpix_mhz = static_cast<double>(val[8]) / 1000.0;
        if (cnt == 11) {

            m.hpol = (val[9] != 0);
            m.vpol = (val[10] != 0);
            out.how = ModeRequest::Eleven;
        } else {
            out.how = ModeRequest::Nine;
        }
        return out;
    }

    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(cnt)});
}

namespace {

VideoService::StoredMode apply_preset(std::uint32_t mode, bool supports_pr) {
    if (mode >= kNumVmodes) mode = 0;

    if (kVmodes[mode].pr == 1 && !supports_pr) mode = 8;

    const PresetMode& p = kVmodes[mode];
    VideoService::StoredMode out{};
    out.mode.hact = p.vpar[0];
    out.mode.hfp = p.vpar[1];
    out.mode.hs = p.vpar[2];
    out.mode.hbp = p.vpar[3];
    out.mode.vact = p.vpar[4];
    out.mode.vfp = p.vpar[5];
    out.mode.vs = p.vpar[6];
    out.mode.vbp = p.vpar[7];
    out.mode.vic = p.vic;
    out.mode.pixel_repeat = (p.pr != 0);
    out.mode.rb = 1;
    out.mode.fpix_mhz = p.fpix_mhz;
    return out;
}

}  // namespace

VideoService::StoredMode VideoService::store_mode(const ParsedMode& parsed, bool support_fhd,
                                                  bool supports_pr) {
    if (parsed.how != ModeRequest::PresetIndex) {

        StoredMode out{};
        out.mode = parsed.mode;
        out.fully_custom = true;
        out.explicitly_requested = false;
        return out;
    }
    (void)support_fhd;
    StoredMode out = apply_preset(parsed.preset_index, supports_pr);

    out.mode.hpol = parsed.mode.hpol;
    out.mode.vpol = parsed.mode.vpol;
    out.explicitly_requested = true;
    return out;
}

VideoService::StoredMode VideoService::preset_fallback(bool support_fhd, bool supports_pr) {
    StoredMode out = apply_preset(support_fhd ? 8u : 0u, supports_pr);
    out.explicitly_requested = false;
    return out;
}

Ex<void> VideoService::apply(const Modeline& m) {

    if (sink_ == nullptr) return err(Errc::negotiation, ERR_SITE());

    if (m.fpix_mhz > 0.0) {
        auto p = pll::solve(m.fpix_mhz);
        if (!p) {
            return err(Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(m.fpix_mhz));
        }
        pll_ = pll::block(*p);
    }

    Modeline::WireOptions opt = wire_opt_;

    opt.vsync_align = opt.vsync_align && (m.fpix_mhz > 0.0);

    const std::array<std::uint16_t, 26> words = m.to_wire(pll_, opt);
    return sink_->publish(kUioSetVideo, std::span<const std::uint16_t>(words.data(), words.size()));
}

VFilter VideoService::select_vfilter(const VideoSample& s, std::span<const bool> slot_enabled) {

    const auto has = [&](VFilter f) {
        const auto i = static_cast<std::size_t>(f);
        return i < slot_enabled.size() && slot_enabled[i];
    };
    if (s.interlaced()) {
        return has(VFilter::Ilace) ? VFilter::Ilace : VFilter::Horz;
    }
    if ((s.flt_flags & 0x30u) && has(VFilter::Scanlines)) return VFilter::Scanlines;
    if (has(VFilter::Vert)) return VFilter::Vert;
    return VFilter::Horz;
}

void VideoService::set_filter_slots(std::span<const bool> enabled) noexcept {
    for (std::size_t i = 0; i < filter_slots_.size(); ++i) {
        filter_slots_[i] = (i < enabled.size()) && enabled[i];
    }
}

Ex<void> VideoService::poll_resolution() {

    if (sampler_ == nullptr) return err(Errc::negotiation, ERR_SITE());

    const bool force = force_next_;
    auto s = sampler_->sample(force);
    if (!s) return err(s.error().code, ERR_SITE(), s.error().detail);
    force_next_ = false;

    const bool res_changed = (s->res != last_res_);
    last_res_ = s->res;
    if (res_changed) vi_seen_ = true;

    const bool fb_changed = (s->fb_crc != last_fb_crc_);
    last_fb_crc_ = s->fb_crc;

    info_ = *s;

    const bool changed = vi_seen_ && (res_changed || fb_changed);
    if (changed) ++res_gen_.v;

    const bool flt_force = changed;
    if (s->flt_flags != 0 && (flt_force || last_flt_flags_ != s->flt_flags)) {
        last_flt_flags_ = s->flt_flags;
        vfilter_ = select_vfilter(info_, std::span<const bool>(filter_slots_));
    }

    return {};
}

std::optional<double> VideoService::estimate_fpix(const Modeline& m, std::uint32_t vtime,
                                                  double refresh_min, double refresh_max) {

    if (vtime == 0) return std::nullopt;
    const double htotal = static_cast<double>(m.hact + m.hfp + m.hs + m.hbp);
    const double vtotal = static_cast<double>(m.vact + m.vfp + m.vs + m.vbp);
    double fpix = 100.0 * htotal * vtotal;
    fpix /= static_cast<double>(vtime);

    if (fpix < kFpixMinMhz || fpix > kFpixMaxMhz) return std::nullopt;

    const float hz = 100000000.0f / static_cast<float>(vtime);
    if (refresh_min != 0.0 && hz < static_cast<float>(refresh_min)) return std::nullopt;
    if (refresh_max != 0.0 && hz > static_cast<float>(refresh_max)) return std::nullopt;
    return fpix;
}

std::optional<VideoService::ScanrateLock> VideoService::lock_scanrate(
    const Modeline& m, std::uint32_t vtime) noexcept {
    if (vtime == 0) return std::nullopt;
    const std::int64_t htotal = static_cast<std::int64_t>(m.hact) + m.hfp + m.hs + m.hbp;
    if (htotal <= 0 || !(m.fpix_mhz > 0.0)) return std::nullopt;

    const std::int64_t vbp = static_cast<std::int64_t>(m.vbp);
    const std::int64_t min_vbp = (vbp < kScanlockMinVbp) ? vbp : kScanlockMinVbp;
    const std::int64_t vt_min = static_cast<std::int64_t>(m.vact) + m.vs + min_vbp + 1;

    const double vtotal_real =
        m.fpix_mhz * static_cast<double>(vtime) / (static_cast<double>(htotal) * 100.0);

    if (!(vtotal_real >= 0.0 && vtotal_real < kScanlockVtotalRoundCeiling)) return std::nullopt;
    const std::int64_t vtotal = std::llround(vtotal_real);
    if (vt_min > kScanlockVtotalMax || vtotal < vt_min) return std::nullopt;

    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.vact) - m.vs;
    const std::int64_t vfp = (vblank - vbp < 1) ? 1 : vblank - vbp;

    ScanrateLock out{};
    out.vfp = static_cast<std::uint32_t>(vfp);
    out.vbp = static_cast<std::uint32_t>(vblank - vfp);
    out.vtotal = static_cast<std::uint32_t>(vtotal);
    out.target_scanrate_hz = (m.fpix_mhz * 1000000.0) / static_cast<double>(htotal);
    out.actual_scanrate_hz =
        static_cast<double>(vtotal) * (100000000.0 / static_cast<double>(vtime));
    return out;
}

bool VideoService::rotation_needs_buffered_scaler(const Modeline& m,
                                                  const std::optional<ScanrateLock>& lock,
                                                  const VideoSample& s) noexcept {
    if (!s.rotated() || s.htime == 0 || s.vtime == 0) return false;
    const std::uint32_t vfp = lock ? lock->vfp : m.vfp;
    const std::uint32_t vbp = lock ? lock->vbp : m.vbp;
    const std::int64_t vtotal = static_cast<std::int64_t>(m.vact) + vfp + m.vs + vbp;
    if (vtotal <= 0) return false;
    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.vact);
    return (vblank * static_cast<std::int64_t>(s.vtime) / vtotal) >
           (static_cast<std::int64_t>(s.de_v) + kScanlockRotMargin) *
               static_cast<std::int64_t>(s.htime);
}

VideoService::FamilyChoice VideoService::select_family(std::uint32_t vtime, bool vsync_adjust,
                                                       bool have_pal, bool have_ntsc) noexcept {

    FamilyChoice out{};
    if (vtime == 0 || !vsync_adjust) return out;
    if (have_pal || have_ntsc) {
        if (vtime > kPalVtimeTicks) {
            out.family = have_pal ? VmodeFamily::Pal : VmodeFamily::Ntsc;
            out.adjustable = have_pal;
        } else {
            out.family = have_ntsc ? VmodeFamily::Ntsc : VmodeFamily::Pal;
            out.adjustable = have_ntsc;
        }
        return out;
    }
    out.adjustable = true;
    return out;
}

Ex<void> VideoService::poll_hotplug() {

    if (!io_.hdmi || !io_.main) {
        return err(Errc::negotiation, ERR_SITE());
    }

    if (!hdmi_capable_) return {};

    if (auto c = cec_.poll_deadline(); !c) {
        return err(c.error().code, ERR_SITE(), c.error().detail);
    }

    if (!io_.hdmi->hdmi_int_asserted()) return {};

    auto irq = io_.main->read(adv7513::kRegIntStatus);

    if (!irq) return err(irq.error().code, ERR_SITE(), irq.error().detail);
    if (*irq == 0) return {};

    const std::uint8_t clear_mask = static_cast<std::uint8_t>(*irq & 0xC0u);
    if (clear_mask == 0) return {};
    if (auto r = io_.main->write(adv7513::kRegIntStatus, clear_mask); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    auto status = io_.main->read(adv7513::kRegStatus);
    if (!status) return err(status.error().code, ERR_SITE(), status.error().detail);
    const bool hpd_high = (*status & 0x40u) != 0;
    const bool ms_high = (*status & 0x20u) != 0;

    if (hpd_high && ms_high) {

        init_owed_ = true;
        if (!power_requested_) return {};
        if (auto r = apply_tmds_power(true); !r) return r;
        return settle_and_publish();
    }

    return apply_tmds_power(false);
}

Ex<void> VideoService::apply_tmds_power(bool on) {
    const adv7513::RegWrite w = adv7513::power(on);
    if (auto r = io_.main->write(w.reg, w.val); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    sink_powered_ = on;
    return {};
}

Ex<void> VideoService::settle_and_publish() {
    auto st = io_.main->read(adv7513::kRegStatus);
    if (!st) return err(st.error().code, ERR_SITE(), st.error().detail);
    if ((*st & 0x40u) == 0 || (*st & 0x20u) == 0) return {};
    ++hpd_gen_.v;
    return {};
}

Ex<void> VideoService::request_power(bool on) {
    if (!io_.main) {
        return err(Errc::negotiation, ERR_SITE());
    }
    power_requested_ = on;
    if (auto r = apply_tmds_power(on); !r) return r;
    if (!on) return {};

    if (init_owed_) return settle_and_publish();
    ++audio_gen_.v;
    return {};
}

Ex<void> emit_video_words(hal::ISpiTransport& link, std::uint8_t opcode,
                          std::span<const std::uint16_t> words) {
    if (words.empty() || words.size() > kVideoWireWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }

    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{opcode}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : words) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

namespace {

inline Ex<std::uint16_t> read_word(hal::ISpiTransport& link) {
    auto r = link.transfer(hal::SpiWord{0});
    if (!r) return std::unexpected(r.error());
    return r->v;
}

inline Ex<std::uint32_t> read_dword(hal::ISpiTransport& link) {
    auto lo = read_word(link);
    if (!lo) return std::unexpected(lo.error());
    auto hi = read_word(link);
    if (!hi) return std::unexpected(hi.error());
    return static_cast<std::uint32_t>(*lo) | (static_cast<std::uint32_t>(*hi) << 16);
}

inline Ex<void> write_bytes(hal::ISpiTransport& link, std::span<const std::uint8_t> b) {
    for (const std::uint8_t byte : b) {
        auto r = link.transfer(hal::SpiWord{byte});
        if (!r) return std::unexpected(r.error());
    }
    return {};
}

}  // namespace

Ex<std::uint16_t> emit_fbuf(hal::ISpiTransport& link, std::uint16_t word) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    auto op = link.transfer(hal::SpiWord{kUioSetFbuf});
    if (!op) return std::unexpected(op.error());
    if (auto r = link.transfer(hal::SpiWord{word}); !r) {
        return std::unexpected(r.error());
    }

    return op->v;
}

Ex<void> emit_ar_custom(hal::ISpiTransport& link, std::span<const std::uint16_t> words) {
    if (words.size() != 4) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kUioSetArCust}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : words) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

Ex<std::uint16_t> emit_filter_mode(hal::ISpiTransport& link, std::uint8_t mode) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioSetFltNum});
    if (!op) return std::unexpected(op.error());

    if (op->v == 0) return std::uint16_t{0};

    const std::uint8_t b[1] = {mode};
    if (auto r = write_bytes(link, std::span<const std::uint8_t>(b, 1)); !r) {
        return std::unexpected(r.error());
    }
    return op->v;
}

Ex<void> emit_filter_coeff_chunk(hal::ISpiTransport& link, std::span<const std::uint16_t> chunk) {

    if (chunk.size() > kFilterCoeffWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(chunk.size()));
    }
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kUioSetFltCoef}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : chunk) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

Ex<std::uint16_t> emit_shadow_mask(hal::ISpiTransport& link, std::uint16_t flag_word) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioShadowMask});
    if (!op) return std::unexpected(op.error());

    if (op->v == 0) return std::uint16_t{0};
    if (auto r = link.transfer(hal::SpiWord{flag_word}); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = link.transfer(hal::SpiWord{0}); !r) {
        return std::unexpected(r.error());
    }
    return op->v;
}

Ex<std::uint16_t> emit_hdmi_int_probe(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioHdmiInt});
    if (!op) return std::unexpected(op.error());
    return op->v;
}

Ex<std::uint16_t> emit_gamma_probe(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioSetGamma});
    if (!op) return std::unexpected(op.error());
    return op->v;
}

ScalingWords scaling_words(const VideoSample& s, const ScalingPolicy& pol) noexcept {
    ScalingWords out{};
    if (pol.vscale_mode >= 4) return out;
    const std::uint32_t height = s.rotated() ? s.width : s.height;
    if (pol.scrh != 0) {
        std::uint32_t scrh = pol.scrh;
        if (pol.vscale_mode != 0 && height != 0) {
            const std::uint32_t div = 1u << (pol.vscale_mode - 1u);
            const std::uint32_t mag = (scrh * div) / height;
            scrh = (height * mag) / div;
            out.height = static_cast<std::uint16_t>(scrh);
        } else if (pol.vscale_border != 0) {
            std::uint32_t border = pol.vscale_border * 2u;
            if ((border + 100u) > scrh) border = scrh - 100u;
            scrh -= border;
            out.height = static_cast<std::uint16_t>(scrh);
        }
    }
    if (pol.scrw != 0) {
        if (pol.vscale_border != 0 && !(pol.vscale_mode != 0 && height != 0)) {
            std::uint32_t scrw = pol.scrw;
            std::uint32_t border = pol.vscale_border * 2u;
            if ((border + 100u) > scrw) border = scrw - 100u;
            scrw -= border;
            out.width = static_cast<std::uint16_t>(scrw);
        }
    }
    return out;
}

Ex<GeometryPass> sample_video_geometry(hal::ISpiTransport& link, bool force, GeometryGate& gate,
                                       IFilterResend* filters) {
    GeometryPass pass{};
    pass.sample = gate.last;

    std::uint16_t commit_res = gate.last_res;
    std::uint8_t commit_fb_crc = gate.last_fb_crc;
    std::uint16_t commit_flt = gate.last_flt_flags;
    bool commit_vi_seen = gate.vi_seen;

    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        if (auto r = link.transfer(hal::SpiWord{kUioGetVres}); !r) {
            return std::unexpected(r.error());
        }
        auto res = read_word(link);
        if (!res) return std::unexpected(res.error());
        pass.res_changed = (*res != gate.last_res);
        pass.sample.res = *res;

        commit_res = *res;
        if (pass.res_changed || force) {
            if (pass.res_changed) commit_vi_seen = true;
            auto w32 = [&](std::uint32_t& dst) -> Ex<void> {
                auto v = read_dword(link);
                if (!v) return std::unexpected(v.error());
                dst = *v;
                return {};
            };
            auto w16 = [&](std::uint16_t& dst) -> Ex<void> {
                auto v = read_word(link);
                if (!v) return std::unexpected(v.error());
                dst = *v;
                return {};
            };
            if (auto r = w32(pass.sample.width); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.height); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.htime); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.vtime); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.ptime); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.vtimeh); !r) return std::unexpected(r.error());
            if (auto r = w32(pass.sample.ctime); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.pixrep); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.de_h); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.de_v); !r) return std::unexpected(r.error());

            auto fc_lo = read_word(link);
            if (!fc_lo) return std::unexpected(fc_lo.error());
            auto fc_hi = read_word(link);
            if (!fc_hi) return std::unexpected(fc_hi.error());
            pass.sample.frame_clocks = static_cast<std::uint32_t>(*fc_lo) |
                                       ((static_cast<std::uint32_t>(*fc_hi) & 0xFFu) << 16);
        }
    }

    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        auto op = link.transfer(hal::SpiWord{kUioGetFbPar});
        if (!op) return std::unexpected(op.error());
        const auto crc = static_cast<std::uint8_t>(op->v & 0xFFu);
        pass.fb_changed = (crc != gate.last_fb_crc);
        commit_fb_crc = crc;
        pass.sample.fb_crc = crc;
        if (pass.fb_changed || force || pass.res_changed) {
            auto w16 = [&](std::uint16_t& dst) -> Ex<void> {
                auto v = read_word(link);
                if (!v) return std::unexpected(v.error());
                dst = *v;
                return {};
            };
            if (auto r = w16(pass.sample.arx_raw); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.ary_raw); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.fb_fmt); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.fb_width); !r) return std::unexpected(r.error());
            if (auto r = w16(pass.sample.fb_height); !r) return std::unexpected(r.error());
        }
    }

    pass.changed = commit_vi_seen && (pass.res_changed || pass.fb_changed);

    if (pass.changed || force) {
        hal::Selected cs(link, hal::ChipSelect::Io);
        const std::uint8_t b[2] = {kUioSetYcPar, 0x00};
        if (auto r = write_bytes(link, std::span<const std::uint8_t>(b, 2)); !r) {
            return std::unexpected(r.error());
        }
        pass.yc_sent = true;
    }

    const bool resend = pass.changed && !gate.policy.front_end;
    bool vfilter_gate_open = false;
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        auto op = link.transfer(hal::SpiWord{kUioSetFltNum});
        if (!op) return std::unexpected(op.error());
        pass.sample.flt_flags = op->v;
        if (op->v != 0 && (resend || gate.last_flt_flags != op->v)) {
            commit_flt = op->v;
            const std::uint8_t mode[1] = {gate.policy.filter_mode};
            if (auto r = write_bytes(link, std::span<const std::uint8_t>(mode, 1)); !r) {
                return std::unexpected(r.error());
            }
            pass.resent = true;
            vfilter_gate_open = true;
        }
    }

    bool tail_deferred = false;
    if (vfilter_gate_open && filters != nullptr) {
        tail_deferred = filters->plan_filter_send(pass.sample, resend);
    }

    if (vfilter_gate_open && !tail_deferred) {
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto r = link.transfer(hal::SpiWord{kUioSetFltCoef}); !r) {
            return std::unexpected(r.error());
        }
    }

    if (resend && !tail_deferred) {

        const ScalingWords sw = scaling_words(pass.sample, gate.policy);
        const std::uint16_t hpay = sw.height;
        const std::uint16_t wpay = sw.width;

        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{kUioSetHeight}); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link.transfer(hal::SpiWord{hpay}); !r) {
                return std::unexpected(r.error());
            }
        }
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{kUioSetWidth}); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link.transfer(hal::SpiWord{wpay}); !r) {
                return std::unexpected(r.error());
            }
        }
    }

    gate.last_res = commit_res;
    gate.last_fb_crc = commit_fb_crc;
    gate.last_flt_flags = commit_flt;
    gate.vi_seen = commit_vi_seen;
    gate.last = pass.sample;
    return pass;
}

}  // namespace mister::svc
