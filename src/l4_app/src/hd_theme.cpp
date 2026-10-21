// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>

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
    return t;
}

}
