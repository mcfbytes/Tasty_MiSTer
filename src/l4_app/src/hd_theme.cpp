// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_theme.h"

#include "app/vec3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>

namespace mister::app {
namespace {

[[nodiscard]] Rgb888 unpack_565(std::uint16_t px) noexcept {
    constexpr unsigned kRedPlace = 11;
    constexpr unsigned kGreenPlace = 5;
    constexpr unsigned kGreenBits = 0x3F;
    constexpr unsigned kBlueBits = 0x1F;
    constexpr unsigned kRedUp = 3;
    constexpr unsigned kGreenUp = 2;
    constexpr unsigned kBlueUp = 3;
    constexpr unsigned kRedRepeat = 2;
    constexpr unsigned kGreenRepeat = 4;
    constexpr unsigned kBlueRepeat = 2;
    const unsigned r = px >> kRedPlace;
    const unsigned g = (px >> kGreenPlace) & kGreenBits;
    const unsigned b = px & kBlueBits;
    return rgb_from_channels(
        {static_cast<std::uint8_t>((r << kRedUp) | (r >> kRedRepeat)),
         static_cast<std::uint8_t>((g << kGreenUp) | (g >> kGreenRepeat)),
         static_cast<std::uint8_t>((b << kBlueUp) | (b >> kBlueRepeat))}
    );
}

using Rgb = std::array<std::uint8_t, 3>;

struct Lch {
    double l = 0.0;
    double c = 0.0;
    double h = 0.0;
};

constexpr double kByteMax = 255.0;

constexpr float kByteMaxF = 255.f;
constexpr std::uint8_t kChannelMax = 255;
constexpr float kHueSectorDeg = 60.f;
constexpr float kHueTurnDeg = 360.f;
constexpr std::size_t kHueSectors = 6;

constexpr float kGreenSector = 2.f;
constexpr float kBlueSector = 4.f;
constexpr float kSectorCycle = 2.f;
constexpr float kHalf = 0.5f;

constexpr double kSrgbToLinearKnee = 0.04045;
constexpr double kSrgbFromLinearKnee = 0.0031308;
constexpr double kSrgbLinearSlope = 12.92;
constexpr double kSrgbOffset = 0.055;
constexpr double kSrgbScale = 1.055;
constexpr double kSrgbGamma = 2.4;
constexpr double kRoundHalf = 0.5;

constexpr std::array<double, 3> kLuma{0.2126, 0.7152, 0.0722};

constexpr Mat3 kLmsOfLinear{std::array<Vec3<double>, 3>{
    {{0.4122214708, 0.5363325363, 0.0514459929},
     {0.2119034982, 0.6806995451, 0.1073969566},
     {0.0883024619, 0.2817188376, 0.6299787005}}
}};
constexpr Mat3 kLabOfLms{std::array<Vec3<double>, 3>{
    {{0.2104542553, 0.7936177850, -0.0040720468},
     {1.9779984951, -2.4285922050, 0.4505937099},
     {0.0259040371, 0.7827717662, -0.8086757660}}
}};
constexpr std::array<double, 2> kLmsLOfAb{0.3963377774, 0.2158037573};
constexpr std::array<double, 2> kLmsMOfAb{-0.1055613458, -0.0638541728};
constexpr std::array<double, 2> kLmsSOfAb{-0.0894841775, -1.2914855480};
constexpr Mat3 kLinearOfLms3{std::array<Vec3<double>, 3>{
    {{4.0767416621, -3.3077115913, 0.2309699292},
     {-1.2684380046, 2.6097574011, -0.3413193965},
     {-0.0041960863, -0.7034186147, 1.7076147010}}
}};
constexpr double kGamutEps = 1e-6;

constexpr Rgb888 kBlack{0x000000u};
constexpr Rgb888 kWhite{0xffffffu};

Rgb unpack(Rgb888 c) noexcept {
    return c.channels();
}

Rgb888 pack(Rgb c) noexcept {
    return rgb_from_channels(c);
}

std::uint8_t mix_ch(std::uint8_t x, std::uint8_t y, float t) noexcept {
    return static_cast<std::uint8_t>(
        static_cast<float>(x) * (1.f - t) + static_cast<float>(y) * t + 0.5f
    );
}

Rgb888 mix(Rgb888 a, Rgb888 b, float t) noexcept {
    const Rgb x = unpack(a);
    const Rgb y = unpack(b);
    return pack({mix_ch(x[0], y[0], t), mix_ch(x[1], y[1], t), mix_ch(x[2], y[2], t)});
}

double to_linear(std::uint8_t v) noexcept {
    const double s = static_cast<double>(v) / kByteMax;
    return s <= kSrgbToLinearKnee ? s / kSrgbLinearSlope
                                  : std::pow((s + kSrgbOffset) / kSrgbScale, kSrgbGamma);
}

std::uint8_t from_linear(double v) noexcept {
    const double c = std::clamp(v, 0.0, 1.0);
    const double s = c <= kSrgbFromLinearKnee
        ? c * kSrgbLinearSlope
        : kSrgbScale * std::pow(c, 1.0 / kSrgbGamma) - kSrgbOffset;
    return static_cast<std::uint8_t>(s * kByteMax + kRoundHalf);
}

double luminance(Rgb888 rgb) noexcept {
    const Rgb c = unpack(rgb);
    return kLuma[0] * to_linear(c[0]) + kLuma[1] * to_linear(c[1]) + kLuma[2] * to_linear(c[2]);
}

Lch to_lch(Rgb888 rgb) noexcept {
    const Rgb c = unpack(rgb);
    const Vec3<double> linear{to_linear(c[0]), to_linear(c[1]), to_linear(c[2])};
    const Vec3<double> lms = mat3_apply(kLmsOfLinear, linear);
    const Vec3<double> root{std::cbrt(lms.x), std::cbrt(lms.y), std::cbrt(lms.z)};
    const Vec3<double> lab = mat3_apply(kLabOfLms, root);
    return {lab.x, std::hypot(lab.y, lab.z), std::atan2(lab.z, lab.y)};
}

Vec3<double> lch_to_linear(Lch x) noexcept {
    const double ok_a = x.c * std::cos(x.h);
    const double ok_b = x.c * std::sin(x.h);
    const double l = x.l + kLmsLOfAb[0] * ok_a + kLmsLOfAb[1] * ok_b;
    const double m = x.l + kLmsMOfAb[0] * ok_a + kLmsMOfAb[1] * ok_b;
    const double s = x.l + kLmsSOfAb[0] * ok_a + kLmsSOfAb[1] * ok_b;
    return mat3_apply(kLinearOfLms3, Vec3<double>{l * l * l, m * m * m, s * s * s});
}

bool in_gamut(const Vec3<double>& v) noexcept {
    const std::array<double, 3> channels{v.x, v.y, v.z};
    return std::all_of(channels.begin(), channels.end(), [](double c) {
        return c >= -kGamutEps && c <= 1.0 + kGamutEps;
    });
}

Rgb888 from_lch(Lch x) noexcept {
    constexpr int kChromaBisections = 24;
    x.l = std::clamp(x.l, 0.0, 1.0);
    if (!in_gamut(lch_to_linear(x))) {
        double lo = 0.0;
        double hi = x.c;
        for (int i = 0; i < kChromaBisections; ++i) {
            const double mid = (lo + hi) / 2.0;
            if (in_gamut(lch_to_linear(Lch{x.l, mid, x.h})))
                lo = mid;
            else
                hi = mid;
        }
        x.c = lo;
    }
    const Vec3<double> v = lch_to_linear(x);
    return pack({from_linear(v.x), from_linear(v.y), from_linear(v.z)});
}

double shown_contrast(Rgb888 a, Rgb888 b) noexcept {
    return contrast_ratio(shown_565(a), shown_565(b));
}

Rgb888 walk_lightness(Rgb888 start, Rgb888 bg, double floor, bool toward_light) noexcept {
    if (shown_contrast(start, bg) >= floor)
        return start;
    const Lch from = to_lch(start);
    constexpr double kStep = 0.002;
    for (double l = from.l; l >= 0.0 && l <= 1.0; l += toward_light ? kStep : -kStep) {
        const Rgb888 c = from_lch(Lch{l, from.c, from.h});
        if (shown_contrast(c, bg) >= floor)
            return c;
    }
    return shown_contrast(kWhite, bg) >= shown_contrast(kBlack, bg) ? kWhite : kBlack;
}

Rgb888 surface(const HdStyle& s, Rgb888 accent, float t, float bias) noexcept {
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
    float x = c * kByteMaxF;
    if (std::fmod(x, 1.f) > kHalf)
        x += kHalf;
    if (!(x > 0.f))
        return 0;
    if (x >= kByteMaxF)
        return kChannelMax;
    return static_cast<std::uint8_t>(x);
}

Hsv to_hsv(Rgb888 rgb) noexcept {
    const Rgb c = unpack(rgb);
    const float r = static_cast<float>(c[0]) / kByteMaxF;
    const float g = static_cast<float>(c[1]) / kByteMaxF;
    const float b = static_cast<float>(c[2]) / kByteMaxF;
    const float hi = std::max({r, g, b});
    const float chroma = hi - std::min({r, g, b});
    float sector = 0.f;
    if (chroma == 0.f)
        sector = 0.f;
    else if (hi == r)
        sector = std::fmod((g - b) / chroma, static_cast<float>(kHueSectors));
    else if (hi == g)
        sector = kGreenSector + (b - r) / chroma;
    else
        sector = kBlueSector + (r - g) / chroma;
    return {rem_euclid(kHueSectorDeg * sector, kHueTurnDeg), hi == 0.f ? 0.f : chroma / hi, hi};
}

Rgb888 from_hsv(Hsv x) noexcept {
    const float chroma = x.s * x.v;
    const float hue = rem_euclid(x.h, kHueTurnDeg);
    const float mid = chroma
        * (1.f - std::fabs(std::fmod(hue / kHueSectorDeg, kSectorCycle) - 1.f));
    const std::array<Vec3f, kHueSectors> by_sector{
        {{chroma, mid, 0.f},
         {mid, chroma, 0.f},
         {0.f, chroma, mid},
         {0.f, mid, chroma},
         {mid, 0.f, chroma},
         {chroma, 0.f, mid}}
    };
    const auto sector = static_cast<std::size_t>(hue / kHueSectorDeg);
    const Vec3f c = sector < by_sector.size() ? by_sector[sector] : Vec3f{};
    const float m = x.v - chroma;
    return pack({slint_quantize(c.x + m), slint_quantize(c.y + m), slint_quantize(c.z + m)});
}

Rgb888 slint_brighter(Rgb888 rgb, float factor) noexcept {
    Hsv x = to_hsv(rgb);
    x.v *= 1.f + factor;
    return from_hsv(x);
}

Rgb888 slint_darker(Rgb888 rgb, float factor) noexcept {
    Hsv x = to_hsv(rgb);
    x.v /= 1.f + factor;
    return from_hsv(x);
}

constexpr float kFocusLift = 1.4f;
constexpr float kWindowFloorValue = 0.30f;
constexpr float kWindowStep = 0.01f;
constexpr float kGreyChroma = 0.04f;
constexpr float kGreyLiftSaturation = 0.5f;
constexpr double kFocusVsBand = 1.4;
constexpr double kGraphicFloor = 3.0;
constexpr double kTextFloor = 4.5;

bool inks_read_on_bands(Rgb888 window, std::initializer_list<Rgb888> inks) noexcept {
    for (int band = 0; band < kWindowBands; ++band)
        for (const Rgb888 ink : inks)
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
        lift.s = a.s * kGreyLiftSaturation;
    }
    const auto inks = {t.text, t.text_label, t.accent};
    Rgb888 window = style.primary;
    for (float v = std::max(authored.v, kWindowFloorValue); v > authored.v; v -= kWindowStep) {
        const Rgb888 c = from_hsv(Hsv{lift.h, lift.s, v});
        if (inks_read_on_bands(c, inks)) {
            window = c;
            break;
        }
    }
    t.window = window;
    const Rgb888 top = window_band(window, 0);
    const Rgb888 lit = slint_brighter(window, kFocusLift);
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

std::uint16_t under_grey(std::uint8_t v, DimQ8 dim) noexcept {
    return hd_pack565(grey_of(dim.apply(ColorChannel{v})));
}

struct GlassPair {
    Rgb888 ink{};
    std::uint16_t fill_pre = 0;
    double floor = 0.0;
};

bool glass_holds(std::span<const GlassPair> pairs, DimQ8 dim) noexcept {
    for (const GlassPair& p : pairs) {
        const Rgb888 ink = shown_565(p.ink);
        const auto at = [&](std::uint8_t v) {
            return unpack_565(static_cast<std::uint16_t>(p.fill_pre + under_grey(v, dim)));
        };
        constexpr std::uint8_t kDarkest = 0;
        constexpr std::uint8_t kLightest = 255;
        const Rgb888 dark = at(kDarkest);
        const Rgb888 light = at(kLightest);
        if ((luminance(ink) > luminance(dark)) != (luminance(ink) > luminance(light)))
            return false;
        if (contrast_ratio(ink, dark) < p.floor || contrast_ratio(ink, light) < p.floor)
            return false;
    }
    return true;
}

Rgb888 pick_key(Rgb888 fill, std::span<const std::uint16_t> taken) noexcept {

    constexpr std::array<Rgb, 7> kFlips{
        {{0, 0, 8}, {0, 4, 0}, {8, 0, 0}, {0, 4, 8}, {8, 0, 8}, {8, 4, 0}, {8, 4, 8}}
    };
    const Rgb base = unpack(fill);
    for (const Rgb& flip : kFlips) {
        const Rgb888 key = pack(
            {static_cast<std::uint8_t>(base[0] ^ flip[0]),
             static_cast<std::uint8_t>(base[1] ^ flip[1]),
             static_cast<std::uint8_t>(base[2] ^ flip[2])}
        );
        if (std::ranges::find(taken, hd_pack565(key)) == taken.end())
            return key;
    }
    return fill;
}

}

double contrast_ratio(Rgb888 a, Rgb888 b) noexcept {
    constexpr double kFlare = 0.05;
    double la = luminance(a);
    double lb = luminance(b);
    if (la < lb)
        std::swap(la, lb);
    return (la + kFlare) / (lb + kFlare);
}

Rgb888 shown_565(Rgb888 rgb) noexcept {
    return unpack_565(hd_pack565(rgb));
}

Rgb888 window_band(Rgb888 window, int band) noexcept {
    constexpr float kBandSpread = 0.6f;
    const int up = kWindowBands - 1 - std::clamp(band, 0, kWindowBands - 1);
    return slint_brighter(
        window,
        kBandSpread * static_cast<float>(up) / static_cast<float>(kWindowBands - 1)
    );
}

Rgb888 settle_565(Rgb888 rgb) noexcept {
    constexpr int kGreenMax6 = 63;
    constexpr int kGreenDrop = 2;
    constexpr int kGreenRepeat = 4;
    constexpr int kGreenWeight = 2;
    constexpr int kWorst = std::numeric_limits<int>::max();
    const Rgb src = unpack(rgb);
    const Rgb on = unpack(shown_565(rgb));
    const int want = kGreenWeight * src[1] - src[0] - src[2];
    const int g6 = src[1] >> kGreenDrop;
    int best_g = on[1];
    int best_err = kWorst;
    for (int g = std::max(0, g6 - 1); g <= std::min(kGreenMax6, g6 + 1); ++g) {
        const int shown_g = (g << kGreenDrop) | (g >> kGreenRepeat);
        const int err = std::abs(kGreenWeight * shown_g - on[0] - on[2] - want);
        if (err < best_err) {
            best_err = err;
            best_g = shown_g;
        }
    }
    return pack({on[0], static_cast<std::uint8_t>(best_g), on[2]});
}

HdTheme derive_theme(HdStyle style) noexcept {
    constexpr float kBarMix = 0.35f;
    constexpr float kPanelTint = 0.05f;
    constexpr float kPanelBias = 0.04f;
    constexpr float kCardTint = 0.08f;
    constexpr float kCardBias = 0.05f;
    constexpr float kSubtleTint = 0.14f;
    constexpr float kSubtleBias = 0.03f;
    constexpr float kMidTint = 0.32f;
    constexpr float kMidBias = 0.05f;
    constexpr float kLabelMix = 0.62f;
    constexpr float kEdgeTint = 0.06f;
    constexpr float kEdgeBias = 0.54f;
    constexpr double kSelectionChroma = 0.7;
    constexpr Rgb888 kErrorOnDark{0xff4f91u};
    constexpr Rgb888 kErrorOnLight{0xc2185bu};
    HdTheme t{};
    const bool up = luminance(style.text) > luminance(style.primary);
    const Rgb888 ink = up ? kBlack : kWhite;
    t.primary = style.primary;
    t.text = style.text;
    t.warning = style.warning;
    t.accent = walk_lightness(style.accent, style.primary, kTextFloor, up);
    t.bg_bar = settle_565(mix(style.primary, ink, kBarMix));
    t.panel = settle_565(surface(style, t.accent, kPanelTint, kPanelBias));
    t.card = settle_565(surface(style, t.accent, kCardTint, kCardBias));
    t.border_subtle = settle_565(surface(style, t.accent, kSubtleTint, kSubtleBias));
    t.border_mid = settle_565(surface(style, t.accent, kMidTint, kMidBias));
    t.text_label = settle_565(mix(style.primary, style.text, kLabelMix));
    const Lch a = to_lch(t.accent);
    t.selection_fill = from_lch(Lch{a.l, a.c * kSelectionChroma, a.h});
    const Rgb888 seed = shown_contrast(style.primary, t.selection_fill)
            >= shown_contrast(style.text, t.selection_fill)
        ? style.primary
        : style.text;
    t.on_accent = walk_lightness(
        seed,
        t.selection_fill,
        kTextFloor,
        luminance(seed) >= luminance(t.selection_fill)
    );
    t.cue_spent = walk_lightness(style.text, style.text, kGraphicFloor, !up);
    t.control_edge = settle_565(
        mix(mix(style.primary, style.text, kEdgeTint), t.accent, kEdgeBias)
    );
    t.error = up ? kErrorOnDark : kErrorOnLight;
    derive_window(style, t);
    t.glass = hd_glass_keys(t, HdGround::Primary, 100);
    return t;
}

Rgb888 hd_ground_fill(const HdTheme& t, HdGround ground) noexcept {
    return ground == HdGround::WindowShade ? slint_darker(t.window, kWindowShade) : t.primary;
}

DimQ8 HdThemeGlass::under_q8() const noexcept {
    constexpr std::uint8_t kPercent = 100;
    const std::uint8_t capped = std::min(opacity, kPercent);
    return DimQ8::of_percents(
        {.open = Percent::of(kPercent - capped), .under = Percent::of(under_dim)}
    );
}

HdThemeGlass hd_glass_keys(const HdTheme& t, HdGround ground, std::uint8_t opacity) noexcept {
    constexpr std::size_t kTakenRoles = 32;
    HdThemeGlass g{};
    g.ground = hd_ground_fill(t, ground);
    g.plate = t.card;
    g.ground_key = g.ground;
    g.plate_key = g.plate;
    constexpr std::uint8_t kPercentFull = 100;
    if (opacity >= kPercentFull)
        return g;
    g.opacity = opacity;
    std::array<std::uint16_t, kTakenRoles> taken{};
    std::size_t n = 0;
    for (const Rgb888 c :
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
    const auto floor_of = [](Rgb888 ink, Rgb888 fill, double want) {
        return std::min(want, shown_contrast(ink, fill));
    };
    const std::array<GlassPair, 4> pairs{{
        {t.text, ground_pre, floor_of(t.text, g.ground, kTextFloor)},
        {t.accent, ground_pre, floor_of(t.accent, g.ground, kTextFloor)},
        {t.text, plate_pre, floor_of(t.text, g.plate, kTextFloor)},
        {t.text_label, plate_pre, floor_of(t.text_label, g.plate, kGraphicFloor)},
    }};
    g.under_dim = 0;
    for (std::uint8_t d = kPercentFull; d > 0; --d) {
        if (glass_holds(pairs, g.with_under(d).under_q8())) {
            g.under_dim = d;
            break;
        }
    }
    return g;
}

HdTint hd_core_tint(const HdTheme& theme, std::string_view core) noexcept {
    constexpr std::uint32_t kHues = 7;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kHueStepDegrees = 15.0;
    constexpr double kHalfTurnDegrees = 180.0;
    constexpr double kHueStep = kHueStepDegrees * kPi / kHalfTurnDegrees;
    constexpr double kDarker = 0.08;

    constexpr std::uint32_t kFnvBasis = 2166136261u;
    constexpr std::uint32_t kFnvPrime = 16777619u;
    constexpr std::uint32_t kMixMulA = 0x85ebca6bu;
    constexpr std::uint32_t kMixMulB = 0xc2b2ae35u;
    constexpr unsigned kMixShiftA = 16;
    constexpr unsigned kMixShiftB = 13;
    constexpr unsigned kSlotShift = 8;
    std::uint32_t h = kFnvBasis;
    for (const char c : core)
        h = (h ^ static_cast<std::uint8_t>(c)) * kFnvPrime;
    h ^= h >> kMixShiftA;
    h *= kMixMulA;
    h ^= h >> kMixShiftB;
    h *= kMixMulB;
    h ^= h >> kMixShiftA;
    const std::uint32_t slot = (h >> kSlotShift) % (2 * kHues);
    const Lch a = to_lch(theme.accent);
    const auto turn = static_cast<double>(
        static_cast<int>(slot % kHues) - static_cast<int>(kHues / 2)
    );
    const double dl = slot < kHues ? 0.0 : -kDarker;
    const Rgb888 seed = from_lch(Lch{a.l + dl, a.c, a.h + turn * kHueStep});
    const Rgb888 ink = shown_contrast(theme.primary, seed) >= shown_contrast(theme.text, seed)
        ? theme.primary
        : theme.text;
    return HdTint{walk_lightness(seed, ink, kTextFloor, luminance(ink) < luminance(seed)), ink};
}

}
