// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace mister::app {
namespace {

using Rgb = std::array<std::uint8_t, 3>;

struct Lch {
    double l = 0.0;
    double c = 0.0;
    double h = 0.0;
};

Rgb unpack(std::uint32_t c) noexcept {
    return {
        static_cast<std::uint8_t>(c >> 16),
        static_cast<std::uint8_t>(c >> 8),
        static_cast<std::uint8_t>(c)
    };
}

std::uint32_t pack(Rgb c) noexcept {
    return (static_cast<std::uint32_t>(c[0]) << 16)
        | (static_cast<std::uint32_t>(c[1]) << 8)
        | c[2];
}

std::uint8_t mix_ch(std::uint8_t x, std::uint8_t y, float t) noexcept {
    return static_cast<std::uint8_t>(
        static_cast<float>(x) * (1.f - t) + static_cast<float>(y) * t + 0.5f
    );
}

std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) noexcept {
    const Rgb x = unpack(a);
    const Rgb y = unpack(b);
    return pack({mix_ch(x[0], y[0], t), mix_ch(x[1], y[1], t), mix_ch(x[2], y[2], t)});
}

double to_linear(std::uint8_t v) noexcept {
    const double s = static_cast<double>(v) / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
}

std::uint8_t from_linear(double v) noexcept {
    const double c = std::clamp(v, 0.0, 1.0);
    const double s = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(s * 255.0 + 0.5);
}

double luminance(std::uint32_t rgb) noexcept {
    const Rgb c = unpack(rgb);
    return 0.2126 * to_linear(c[0]) + 0.7152 * to_linear(c[1]) + 0.0722 * to_linear(c[2]);
}

Lch to_lch(std::uint32_t rgb) noexcept {
    const Rgb c = unpack(rgb);
    const double r = to_linear(c[0]);
    const double g = to_linear(c[1]);
    const double b = to_linear(c[2]);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    const double ok_l = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    const double ok_a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    const double ok_b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return {ok_l, std::hypot(ok_a, ok_b), std::atan2(ok_b, ok_a)};
}

std::array<double, 3> lch_to_linear(Lch x) noexcept {
    const double ok_a = x.c * std::cos(x.h);
    const double ok_b = x.c * std::sin(x.h);
    const double l = x.l + 0.3963377774 * ok_a + 0.2158037573 * ok_b;
    const double m = x.l - 0.1055613458 * ok_a - 0.0638541728 * ok_b;
    const double s = x.l - 0.0894841775 * ok_a - 1.2914855480 * ok_b;
    const double l3 = l * l * l;
    const double m3 = m * m * m;
    const double s3 = s * s * s;
    return {
        4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
        -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
        -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3
    };
}

bool in_gamut(const std::array<double, 3>& v) noexcept {
    constexpr double kEps = 1e-6;
    return std::all_of(v.begin(), v.end(), [](double c) { return c >= -kEps && c <= 1.0 + kEps; });
}

std::uint32_t from_lch(Lch x) noexcept {
    x.l = std::clamp(x.l, 0.0, 1.0);
    if (!in_gamut(lch_to_linear(x))) {
        double lo = 0.0;
        double hi = x.c;
        for (int i = 0; i < 24; ++i) {
            const double mid = (lo + hi) / 2.0;
            if (in_gamut(lch_to_linear(Lch{x.l, mid, x.h})))
                lo = mid;
            else
                hi = mid;
        }
        x.c = lo;
    }
    const auto v = lch_to_linear(x);
    return pack({from_linear(v[0]), from_linear(v[1]), from_linear(v[2])});
}

double shown_contrast(std::uint32_t a, std::uint32_t b) noexcept {
    return contrast_ratio(shown_565(a), shown_565(b));
}

std::uint32_t walk_lightness(
    std::uint32_t start,
    std::uint32_t bg,
    double floor,
    bool toward_light
) noexcept {
    if (shown_contrast(start, bg) >= floor)
        return start;
    const Lch from = to_lch(start);
    constexpr double kStep = 0.002;
    for (double l = from.l; l >= 0.0 && l <= 1.0; l += toward_light ? kStep : -kStep) {
        const std::uint32_t c = from_lch(Lch{l, from.c, from.h});
        if (shown_contrast(c, bg) >= floor)
            return c;
    }
    return shown_contrast(0xffffffu, bg) >= shown_contrast(0x000000u, bg) ? 0xffffffu : 0x000000u;
}

std::uint32_t surface(const HdStyle& s, std::uint32_t accent, float t, float bias) noexcept {
    return mix(mix(s.primary, s.text, t), accent, bias);
}

struct Hsv {
    float h = 0.f;
    float s = 0.f;
    float v = 0.f;
};

float rem_euclid(float x, float m) noexcept {
    const float r = std::fmod(x, m);
    return r < 0.f ? r + m : r;
}

std::uint8_t slint_quantize(float c) noexcept {
    float x = c * 255.f;
    if (std::fmod(x, 1.f) > 0.5f)
        x += 0.5f;
    if (!(x > 0.f))
        return 0;
    if (x >= 255.f)
        return 255;
    return static_cast<std::uint8_t>(x);
}

Hsv to_hsv(std::uint32_t rgb) noexcept {
    const Rgb c = unpack(rgb);
    const float r = static_cast<float>(c[0]) / 255.f;
    const float g = static_cast<float>(c[1]) / 255.f;
    const float b = static_cast<float>(c[2]) / 255.f;
    const float hi = std::max({r, g, b});
    const float chroma = hi - std::min({r, g, b});
    float sector = 0.f;
    if (chroma == 0.f)
        sector = 0.f;
    else if (hi == r)
        sector = std::fmod((g - b) / chroma, 6.f);
    else if (hi == g)
        sector = 2.f + (b - r) / chroma;
    else
        sector = 4.f + (r - g) / chroma;
    return {rem_euclid(60.f * sector, 360.f), hi == 0.f ? 0.f : chroma / hi, hi};
}

std::uint32_t from_hsv(Hsv x) noexcept {
    const float chroma = x.s * x.v;
    const float hue = rem_euclid(x.h, 360.f);
    const float mid = chroma * (1.f - std::fabs(std::fmod(hue / 60.f, 2.f) - 1.f));
    const std::array<std::array<float, 3>, 6> by_sector{
        {{chroma, mid, 0.f},
         {mid, chroma, 0.f},
         {0.f, chroma, mid},
         {0.f, mid, chroma},
         {mid, 0.f, chroma},
         {chroma, 0.f, mid}}
    };
    const auto sector = static_cast<std::size_t>(hue / 60.f);
    const std::array<float, 3> c = sector < by_sector.size() ? by_sector[sector]
                                                             : std::array<float, 3>{};
    const float m = x.v - chroma;
    return pack({slint_quantize(c[0] + m), slint_quantize(c[1] + m), slint_quantize(c[2] + m)});
}

std::uint32_t slint_brighter(std::uint32_t rgb, float factor) noexcept {
    Hsv x = to_hsv(rgb);
    x.v *= 1.f + factor;
    return from_hsv(x);
}

std::uint32_t slint_darker(std::uint32_t rgb, float factor) noexcept {
    Hsv x = to_hsv(rgb);
    x.v /= 1.f + factor;
    return from_hsv(x);
}

constexpr float kFocusLift = 1.4f;
constexpr float kWindowFloorValue = 0.30f;
constexpr float kWindowStep = 0.01f;
constexpr float kGreyChroma = 0.04f;
constexpr double kFocusVsBand = 1.4;
constexpr double kGraphicFloor = 3.0;
constexpr double kTextFloor = 4.5;

bool inks_read_on_bands(std::uint32_t window, std::initializer_list<std::uint32_t> inks) noexcept {
    for (int band = 0; band < kWindowBands; ++band)
        for (const std::uint32_t ink : inks)
            if (shown_contrast(ink, window_band(window, band)) < kTextFloor)
                return false;
    return true;
}

void derive_window(const HdStyle& style, HdTheme& t) noexcept {
    const Hsv authored = to_hsv(style.primary);
    Hsv lift = authored;
    if (authored.s * authored.v < kGreyChroma) {
        const Hsv a = to_hsv(t.accent);
        lift.h = a.h;
        lift.s = a.s * 0.5f;
    }
    const auto inks = {t.text, t.text_label, t.accent};
    std::uint32_t window = style.primary;
    for (float v = std::max(authored.v, kWindowFloorValue); v > authored.v; v -= kWindowStep) {
        const std::uint32_t c = from_hsv(Hsv{lift.h, lift.s, v});
        if (inks_read_on_bands(c, inks)) {
            window = c;
            break;
        }
    }
    t.window = window;
    const std::uint32_t top = window_band(window, 0);
    const std::uint32_t lit = slint_brighter(window, kFocusLift);
    if (shown_contrast(lit, top) >= kFocusVsBand && shown_contrast(t.accent, lit) >= kTextFloor) {
        t.window_focus = lit;
        t.on_window_focus = t.accent;
    } else {
        t.window_focus = t.selection_fill;
        t.on_window_focus = t.on_accent;
    }
    t.window_hand = shown_contrast(t.text, t.window_focus) >= kGraphicFloor ? t.text
                                                                            : t.on_window_focus;
}

constexpr float kWindowShade = 0.5f;

std::uint32_t replicate_565(std::uint16_t c) noexcept {
    const auto r = static_cast<std::uint32_t>(c >> 11);
    const auto g = static_cast<std::uint32_t>((c >> 5) & 0x3Fu);
    const auto b = static_cast<std::uint32_t>(c & 0x1Fu);
    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

std::uint16_t under_grey(std::uint8_t v, std::uint16_t q8) noexcept {
    const auto c = static_cast<std::uint32_t>((static_cast<std::uint32_t>(v) * q8) >> 8);
    return hd_pack565((c << 16) | (c << 8) | c);
}

struct GlassPair {
    std::uint32_t ink = 0;
    std::uint16_t fill_pre = 0;
    double floor = 0.0;
};

bool glass_holds(std::span<const GlassPair> pairs, std::uint16_t q8) noexcept {
    for (const GlassPair& p : pairs) {
        const std::uint32_t ink = shown_565(p.ink);
        const auto at = [&](std::uint8_t v) {
            return replicate_565(static_cast<std::uint16_t>(p.fill_pre + under_grey(v, q8)));
        };
        const std::uint32_t dark = at(0);
        const std::uint32_t light = at(255);
        if ((luminance(ink) > luminance(dark)) != (luminance(ink) > luminance(light)))
            return false;
        if (contrast_ratio(ink, dark) < p.floor || contrast_ratio(ink, light) < p.floor)
            return false;
    }
    return true;
}

std::uint32_t pick_key(std::uint32_t fill, std::span<const std::uint16_t> taken) noexcept {
    constexpr std::array<std::uint32_t, 7>
        kFlips{0x000008u, 0x000400u, 0x080000u, 0x000408u, 0x080008u, 0x080400u, 0x080408u};
    for (const std::uint32_t flip : kFlips) {
        const std::uint32_t key = fill ^ flip;
        if (std::ranges::find(taken, hd_pack565(key)) == taken.end())
            return key;
    }
    return fill;
}

}

double contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept {
    double la = luminance(a);
    double lb = luminance(b);
    if (la < lb)
        std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

std::uint32_t shown_565(std::uint32_t rgb) noexcept {
    const Rgb c = unpack(rgb);
    const auto r = static_cast<std::uint8_t>(c[0] >> 3);
    const auto g = static_cast<std::uint8_t>(c[1] >> 2);
    const auto b = static_cast<std::uint8_t>(c[2] >> 3);
    return pack(
        {static_cast<std::uint8_t>((r << 3) | (r >> 2)),
         static_cast<std::uint8_t>((g << 2) | (g >> 4)),
         static_cast<std::uint8_t>((b << 3) | (b >> 2))}
    );
}

std::uint32_t window_band(std::uint32_t window, int band) noexcept {
    const int up = kWindowBands - 1 - std::clamp(band, 0, kWindowBands - 1);
    return slint_brighter(
        window,
        0.6f * static_cast<float>(up) / static_cast<float>(kWindowBands - 1)
    );
}

std::uint32_t settle_565(std::uint32_t rgb) noexcept {
    const Rgb src = unpack(rgb);
    const Rgb on = unpack(shown_565(rgb));
    const int want = 2 * src[1] - src[0] - src[2];
    const int g6 = src[1] >> 2;
    int best_g = on[1];
    int best_err = 1 << 30;
    for (int g = std::max(0, g6 - 1); g <= std::min(63, g6 + 1); ++g) {
        const int shown_g = (g << 2) | (g >> 4);
        const int err = std::abs(2 * shown_g - on[0] - on[2] - want);
        if (err < best_err) {
            best_err = err;
            best_g = shown_g;
        }
    }
    return pack({on[0], static_cast<std::uint8_t>(best_g), on[2]});
}

HdTheme derive_theme(HdStyle style) noexcept {
    HdTheme t{};
    const bool up = luminance(style.text) > luminance(style.primary);
    const std::uint32_t ink = up ? 0x000000u : 0xffffffu;
    t.primary = style.primary;
    t.text = style.text;
    t.warning = style.warning;
    t.accent = walk_lightness(style.accent, style.primary, 4.5, up);
    t.bg_bar = settle_565(mix(style.primary, ink, 0.35f));
    t.panel = settle_565(surface(style, t.accent, 0.05f, 0.04f));
    t.card = settle_565(surface(style, t.accent, 0.08f, 0.05f));
    t.border_subtle = settle_565(surface(style, t.accent, 0.14f, 0.03f));
    t.border_mid = settle_565(surface(style, t.accent, 0.32f, 0.05f));
    t.text_label = settle_565(mix(style.primary, style.text, 0.62f));
    const Lch a = to_lch(t.accent);
    t.selection_fill = from_lch(Lch{a.l, a.c * 0.7, a.h});
    const std::uint32_t seed = shown_contrast(style.primary, t.selection_fill)
            >= shown_contrast(style.text, t.selection_fill)
        ? style.primary
        : style.text;
    t.on_accent = walk_lightness(
        seed,
        t.selection_fill,
        4.5,
        luminance(seed) >= luminance(t.selection_fill)
    );
    t.cue_spent = walk_lightness(style.text, style.text, 3.0, !up);
    t.control_edge = settle_565(mix(mix(style.primary, style.text, 0.06f), t.accent, 0.54f));
    t.error = up ? 0xff4f91u : 0xc2185bu;
    derive_window(style, t);
    t.glass = hd_glass_keys(t, HdGround::Primary, 100);
    return t;
}

std::uint32_t hd_ground_fill(const HdTheme& t, HdGround ground) noexcept {
    return ground == HdGround::WindowShade ? slint_darker(t.window, kWindowShade) : t.primary;
}

HdThemeGlass hd_glass_keys(const HdTheme& t, HdGround ground, std::uint8_t opacity) noexcept {
    HdThemeGlass g{};
    g.ground = hd_ground_fill(t, ground);
    g.plate = t.card;
    g.ground_key = g.ground;
    g.plate_key = g.plate;
    if (opacity >= 100)
        return g;
    g.opacity = opacity;
    std::array<std::uint16_t, 32> taken{};
    std::size_t n = 0;
    for (const std::uint32_t c :
         {t.primary,
          t.accent,
          t.text,
          t.warning,
          t.bg_bar,
          t.panel,
          t.card,
          t.border_subtle,
          t.border_mid,
          t.text_label,
          t.selection_fill,
          t.on_accent,
          t.cue_spent,
          t.control_edge,
          t.error,
          t.window,
          t.window_focus,
          t.on_window_focus,
          t.window_hand,
          hd_ground_fill(t, HdGround::Primary),
          hd_ground_fill(t, HdGround::WindowShade)})
        taken[n++] = hd_pack565(c);
    for (int band = 0; band < kWindowBands; ++band)
        taken[n++] = hd_pack565(window_band(t.window, band));
    g.ground_key = pick_key(g.ground, std::span{taken}.first(n));
    taken[n++] = hd_pack565(g.ground_key);
    g.plate_key = pick_key(g.plate, std::span{taken}.first(n));

    const std::uint16_t ground_pre = hd_scale_565(hd_pack565(g.ground), opacity);
    const std::uint16_t plate_pre = hd_scale_565(hd_pack565(g.plate), opacity);
    const auto floor_of = [](std::uint32_t ink, std::uint32_t fill, double want) {
        return std::min(want, shown_contrast(ink, fill));
    };
    const std::array<GlassPair, 4> pairs{{
        {t.text, ground_pre, floor_of(t.text, g.ground, kTextFloor)},
        {t.accent, ground_pre, floor_of(t.accent, g.ground, kTextFloor)},
        {t.text, plate_pre, floor_of(t.text, g.plate, kTextFloor)},
        {t.text_label, plate_pre, floor_of(t.text_label, g.plate, kGraphicFloor)},
    }};
    g.under_dim = 0;
    for (int d = 100; d > 0; --d) {
        if (glass_holds(pairs, hd_under_q8(opacity, static_cast<std::uint8_t>(d)))) {
            g.under_dim = static_cast<std::uint8_t>(d);
            break;
        }
    }
    return g;
}

HdTint hd_core_tint(const HdTheme& theme, std::string_view core) noexcept {
    constexpr std::uint32_t kHues = 7;
    constexpr double kHueStep = 15.0 * 3.14159265358979323846 / 180.0;
    constexpr double kDarker = 0.08;
    std::uint32_t h = 2166136261u;
    for (const char c : core)
        h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    const std::uint32_t slot = (h >> 8) % (2 * kHues);
    const Lch a = to_lch(theme.accent);
    const auto turn = static_cast<double>(
        static_cast<int>(slot % kHues) - static_cast<int>(kHues / 2)
    );
    const double dl = slot < kHues ? 0.0 : -kDarker;
    const std::uint32_t seed = from_lch(Lch{a.l + dl, a.c, a.h + turn * kHueStep});
    const std::uint32_t ink = shown_contrast(theme.primary, seed)
            >= shown_contrast(theme.text, seed)
        ? theme.primary
        : theme.text;
    return HdTint{walk_lightness(seed, ink, kTextFloor, luminance(ink) < luminance(seed)), ink};
}

}
