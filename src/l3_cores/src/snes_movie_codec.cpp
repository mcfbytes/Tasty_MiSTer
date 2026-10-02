// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_movie_codec.h"

#include <bit>

namespace mister::cores {
namespace {

struct SettingRule {
    std::uint8_t lo;
    std::uint8_t width;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr std::array<SettingRule, 8> kRules{{
    {7, 1, 0x01, "Swap Joysticks"},
    {8, 1, 0x01, "SNAC"},
    {43, 1, 0x01, "Miracle Piano"},
    {17, 1, 0x01, "Multitap"},
    {5, 2, 0x01, "Mouse"},
    {25, 2, 0x01, "Super Scope"},
    {4, 1, 0x01, "CPU Speed"},
    {18, 1, 0x01, "SuperFX Speed"},
}};

constexpr std::uint8_t kVideoRegionLo = 14;
constexpr std::uint8_t kInitialWramLo = 21;

}  // namespace

IMovieCodec::SettingNeeds SnesMovieCodec::setting_needs(const Facts& f) const noexcept {
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add({.lo = r.lo,
                 .width = r.width,
                 .allowed = r.allowed,
                 .preferred = static_cast<std::uint8_t>(std::countr_zero(r.allowed)),
                 .name = r.name});
    }

    const std::uint8_t want = f.region == Region::Pal ? 2u : 1u;
    out.add({.lo = kVideoRegionLo,
             .width = 2,
             .allowed = static_cast<std::uint16_t>(1u << want),
             .preferred = want,
             .name = "Video Region"});
    if (const auto fill = do_recorder_wram(f)) {
        const auto v = static_cast<std::uint8_t>(*fill);
        out.add({.lo = kInitialWramLo,
                 .width = 2,
                 .allowed = static_cast<std::uint16_t>(1u << v),
                 .preferred = v,
                 .name = "Initial WRAM"});
    }
    return out;
}

IMovieCodec::Raster SnesMovieCodec::raster(const Facts& f) const noexcept {

    if (f.region == Region::Pal) return {.period_ns = 19'997'209, .line0_ns = 3'277'233};
    return {.period_ns = 16'639'266, .line0_ns = 1'596'106};
}

IMovieCodec::PowerOn SnesMovieCodec::power_on(const Facts&) const noexcept {

    return {.p0 = Parity::Odd};
}

std::optional<IMovieCodec::DigestSpan> SnesMovieCodec::past_header(std::uint64_t size,
                                                                   std::uint64_t header) noexcept {
    if (size < header || size - header < kRomMin) return std::nullopt;
    return DigestSpan{.offset = header, .length = size - header};
}

std::string_view SnesMovieCodec::trim(std::string_view s) noexcept {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

SnesMovieCodec::KeyValue SnesMovieCodec::split(std::string_view line) noexcept {
    line = trim(line);
    const std::size_t sp = line.find(' ');
    if (sp == std::string_view::npos) return {line, {}};
    return {line.substr(0, sp), trim(line.substr(sp + 1))};
}

}  // namespace mister::cores
