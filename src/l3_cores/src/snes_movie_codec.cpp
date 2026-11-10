// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_movie_codec.h"

#include <bit>

namespace mister::cores {
namespace {

struct SettingRule {
    proto::BitField field;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr std::array<SettingRule, 8> kRules{{
    {{proto::StatusBit{7}, 1}, 0x01, "Swap Joysticks"},
    {{proto::StatusBit{8}, 1}, 0x01, "SNAC"},
    {{proto::StatusBit{43}, 1}, 0x01, "Miracle Piano"},
    {{proto::StatusBit{17}, 1}, 0x01, "Multitap"},
    {{proto::StatusBit{5}, 2}, 0x01, "Mouse"},
    {{proto::StatusBit{25}, 2}, 0x01, "Super Scope"},
    {{proto::StatusBit{4}, 1}, 0x01, "CPU Speed"},
    {{proto::StatusBit{18}, 1}, 0x01, "SuperFX Speed"},
}};

constexpr proto::BitField kVideoRegionField{proto::StatusBit{14}, 2};
constexpr proto::BitField kInitialWramField{proto::StatusBit{21}, 2};

}

IReplayCore::SettingNeeds SnesMovieCodec::setting_needs(const MovieFacts& f) const noexcept {
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add(
            {.field = r.field,
             .allowed = proto::ValueSet{r.allowed},
             .preferred = proto::FieldValue{static_cast<std::uint32_t>(
                 std::countr_zero(r.allowed)
             )},
             .name = r.name}
        );
    }

    const std::uint8_t want = f.region == MovieRegion::Pal ? 2u : 1u;
    out.add(
        {.field = kVideoRegionField,
         .allowed = proto::ValueSet{static_cast<std::uint16_t>(1u << want)},
         .preferred = proto::FieldValue{want},
         .name = "Video Region"}
    );
    if (f.recorder_ram_fill)
        out.add(SettingNeed::only(kInitialWramField, *f.recorder_ram_fill, "Initial WRAM"));
    return out;
}

IReplayCore::Raster SnesMovieCodec::raster(const MovieFacts& f) const noexcept {

    if (f.region == MovieRegion::Pal)
        return {
            .period_ns = std::chrono::nanoseconds{19'997'209},
            .line0_ns = std::chrono::nanoseconds{3'277'233}
        };
    return {
        .period_ns = std::chrono::nanoseconds{16'639'266},
        .line0_ns = std::chrono::nanoseconds{1'596'106}
    };
}

IReplayCore::PowerOn SnesMovieCodec::power_on(const MovieFacts&) const noexcept {

    return {.p0 = Parity::Odd};
}

std::optional<IMovieFormat::DigestSpan> SnesMovieCodec::past_header(
    proto::FileSize size,
    proto::FileSize header
) noexcept {
    if (size.after_header(header) < proto::FileSize{kRomMin})
        return std::nullopt;
    return span_past(size, header);
}

}
