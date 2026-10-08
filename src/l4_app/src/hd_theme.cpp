// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mister::app {
namespace {

std::array<std::uint8_t, 3> unpack(std::uint32_t c) noexcept {
    return {static_cast<std::uint8_t>(c >> 16), static_cast<std::uint8_t>(c >> 8),
            static_cast<std::uint8_t>(c)};
}

std::uint32_t pack(std::array<std::uint8_t, 3> c) noexcept {
    return (static_cast<std::uint32_t>(c[0]) << 16) | (static_cast<std::uint32_t>(c[1]) << 8) |
           c[2];
}

std::uint8_t mix_ch(std::uint8_t x, std::uint8_t y, float t) noexcept {
    return static_cast<std::uint8_t>(static_cast<float>(x) * (1.f - t) + static_cast<float>(y) * t +
                                     0.5f);
}

std::array<std::uint8_t, 3> mix(std::array<std::uint8_t, 3> a, std::array<std::uint8_t, 3> b,
                                float t) noexcept {
    return {mix_ch(a[0], b[0], t), mix_ch(a[1], b[1], t), mix_ch(a[2], b[2], t)};
}

double luminance(std::array<std::uint8_t, 3> c) noexcept {
    const auto lin = [](std::uint8_t v) {
        const double s = static_cast<double>(v) / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
}

double contrast(std::array<std::uint8_t, 3> a, std::array<std::uint8_t, 3> b) noexcept {
    double la = luminance(a);
    double lb = luminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

std::array<double, 3> rgb_to_hsl(std::array<std::uint8_t, 3> c) noexcept {
    const double r = static_cast<double>(c[0]) / 255.0;
    const double g = static_cast<double>(c[1]) / 255.0;
    const double b = static_cast<double>(c[2]) / 255.0;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    std::array<double, 3> o{0.0, 0.0, (mx + mn) / 2.0};
    if (mx == mn) return o;
    const double d = mx - mn;
    o[1] = o[2] > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    if (mx == r)
        o[0] = (g - b) / d + (g < b ? 6.0 : 0.0);
    else if (mx == g)
        o[0] = (b - r) / d + 2.0;
    else
        o[0] = (r - g) / d + 4.0;
    o[0] *= 60.0;
    return o;
}

std::array<std::uint8_t, 3> hsl_to_rgb(std::array<double, 3> x) noexcept {
    const auto hue2rgb = [](double p, double q, double t) {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 0.5) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };
    double r = x[2];
    double g = x[2];
    double b = x[2];
    if (x[1] != 0.0) {
        const double q = x[2] < 0.5 ? x[2] * (1.0 + x[1]) : x[2] + x[1] - x[2] * x[1];
        const double p = 2.0 * x[2] - q;
        const double h = x[0] / 360.0;
        r = hue2rgb(p, q, h + 1.0 / 3.0);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0 / 3.0);
    }
    return {static_cast<std::uint8_t>(r * 255.0 + 0.5), static_cast<std::uint8_t>(g * 255.0 + 0.5),
            static_cast<std::uint8_t>(b * 255.0 + 0.5)};
}

std::array<std::uint8_t, 3> walk_on_accent(std::array<std::uint8_t, 3> bg,
                                           std::array<std::uint8_t, 3> hint) noexcept {
    if (contrast(hint, bg) >= 4.5) return hint;
    const auto hsl = rgb_to_hsl(hint);
    auto best = hint;
    double best_c = contrast(hint, bg);
    int best_dist = 256;
    const int orig = static_cast<int>(hsl[2] * 255.0 + 0.5);
    for (int i = 0; i <= 255; ++i) {
        auto t = hsl;
        t[2] = static_cast<double>(i) / 255.0;
        const auto c = hsl_to_rgb(t);
        const double cr = contrast(c, bg);
        const int dist = i > orig ? i - orig : orig - i;
        if (cr >= 4.5 && dist < best_dist) {
            best = c;
            best_dist = dist;
            best_c = cr;
        } else if (best_c < 4.5 && cr > best_c) {
            best = c;
            best_c = cr;
        }
    }
    if (best_c < 4.5) {
        const std::array<std::uint8_t, 3> w{255, 255, 255};
        const std::array<std::uint8_t, 3> k{0, 0, 0};
        return contrast(w, bg) >= contrast(k, bg) ? w : k;
    }
    return best;
}

}  // namespace

double contrast_ratio(std::uint32_t a, std::uint32_t b) noexcept {
    return contrast(unpack(a), unpack(b));
}

HdTheme derive_theme(HdStyle style, std::uint8_t fps) noexcept {
    HdTheme t{};
    const auto primary = unpack(style.primary);
    const auto accent = unpack(style.accent);
    const auto text = unpack(style.text);
    t.primary = style.primary;
    t.accent = style.accent;
    t.text = style.text;
    t.warning = style.warning;
    t.panel = pack(mix(primary, text, 0.05f));
    t.card = pack(mix(primary, text, 0.09f));
    t.border = pack(mix(primary, text, 0.20f));
    t.muted = pack(mix(text, primary, 0.30f));
    t.selection = pack(mix(accent, primary, 0.12f));
    t.on_accent = pack(walk_on_accent(unpack(t.selection), text));
    t.backdrop_dim = 0.4f;
    const std::uint32_t f = fps == 0 ? 1u : fps;
    t.fade_ms = std::max(2u * 1000u / f, 150u);
    t.title = 28.f / 720.f;
    t.row = 20.f / 720.f;
    t.pad = 28.f / 720.f;
    return t;
}

}  // namespace mister::app
