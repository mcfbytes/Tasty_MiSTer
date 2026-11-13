// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/ascii.h"
#include "infra/seat.h"

#include "cores/n64_memory_type.h"
#include "cores/n64_options.h"
#include "cores/n64_patch_table.h"
#include "cores/n64_system_type.h"
#include "proto/bit_field.h"

namespace mister::cores::n64 {

enum class Tag : std::uint8_t {
    Eeprom512 = 0,
    Eeprom2k,
    Sram32k,
    Sram96k,
    Flash128k,
    NoEpak,
    Cpak,
    Rpak,
    Tpak,
    Rtc,
    Ntsc,
    Pal,
    Cic6101,
    Cic6102,
    Cic6103,
    Cic6105,
    Cic6106,
    Cic7101,
    Cic7102,
    Cic7103,
    Cic7105,
    Cic7106,
    Cic8303,
    Cic8401,
    Cic5167,
    CicDdus,
    Cic5101,
    Unknown,
};

inline constexpr std::size_t kTagCount = static_cast<std::size_t>(Tag::Unknown);

[[nodiscard]] constexpr std::string_view tag_name(Tag t) noexcept {
    switch (t) {
        case Tag::Eeprom512:
            return "eeprom512";
        case Tag::Eeprom2k:
            return "eeprom2k";
        case Tag::Sram32k:
            return "sram32k";
        case Tag::Sram96k:
            return "sram96k";
        case Tag::Flash128k:
            return "flash128k";
        case Tag::NoEpak:
            return "noepak";
        case Tag::Cpak:
            return "cpak";
        case Tag::Rpak:
            return "rpak";
        case Tag::Tpak:
            return "tpak";
        case Tag::Rtc:
            return "rtc";
        case Tag::Ntsc:
            return "ntsc";
        case Tag::Pal:
            return "pal";
        case Tag::Cic6101:
            return "cic6101";
        case Tag::Cic6102:
            return "cic6102";
        case Tag::Cic6103:
            return "cic6103";
        case Tag::Cic6105:
            return "cic6105";
        case Tag::Cic6106:
            return "cic6106";
        case Tag::Cic7101:
            return "cic7101";
        case Tag::Cic7102:
            return "cic7102";
        case Tag::Cic7103:
            return "cic7103";
        case Tag::Cic7105:
            return "cic7105";
        case Tag::Cic7106:
            return "cic7106";
        case Tag::Cic8303:
            return "cic8303";
        case Tag::Cic8401:
            return "cic8401";
        case Tag::Cic5167:
            return "cic5167";
        case Tag::CicDdus:
            return "cicddus";
        case Tag::Cic5101:
            return "cic5101";
        case Tag::Unknown:
            break;
    }
    return {};
}

namespace detail {

[[nodiscard]] constexpr std::array<std::string_view, kTagCount> build_tag_names() noexcept {
    std::array<std::string_view, kTagCount> a{};
    for (std::size_t i = 0; i < kTagCount; ++i)
        a[i] = tag_name(static_cast<Tag>(i));
    return a;
}

}

inline constexpr std::array<std::string_view, kTagCount> kTagNames = detail::build_tag_names();

static_assert(
    kTagNames[0] == "eeprom512" && kTagNames[kTagCount - 1] == "cic5101",
    "the Tag order and its spellings have come apart"
);

inline constexpr std::string_view kTagSeparators = "| ";

struct FeatureRow {
    Tag tag;
    proto::BitField field;
    std::string_view info;
    std::optional<PadType> pad;
    bool from_header;
};

inline constexpr std::array<FeatureRow, 5> kFeatureRows{{
    {Tag::Tpak, opt::kTpak, "\nTransfer Pak \x96", PadType::PadWithTpak, true},
    {Tag::Cpak, opt::kCpak, "\nController Pak \x96", PadType::PadWithCpak, true},
    {Tag::Rpak, opt::kRpak, "\nRumble Pak \x96", PadType::PadWithRpak, true},
    {Tag::Rtc, opt::kRtc, "\nRTC \x96", std::nullopt, true},
    {Tag::NoEpak, opt::kNoEpak, "\nDisable Exp. Pak \x96", std::nullopt, false},
}};

[[nodiscard]] constexpr const FeatureRow* feature_row(Tag t) noexcept {
    for (std::size_t i = 0; i < kFeatureRows.size(); ++i)
        if (kFeatureRows[i].tag == t)
            return &kFeatureRows[i];
    return nullptr;
}

namespace detail {

[[nodiscard]] consteval bool feature_rows_are_one_per_tag() noexcept {
    for (std::size_t i = 0; i < kFeatureRows.size(); ++i) {
        const FeatureRow& r = kFeatureRows[i];
        if (r.info.empty() || !feature_row(r.tag) || !r.field.valid())
            return false;
        for (std::size_t j = i + 1; j < kFeatureRows.size(); ++j) {
            const FeatureRow& o = kFeatureRows[j];
            if (r.tag == o.tag || r.field.start == o.field.start)
                return false;
        }
    }
    return true;
}

}

static_assert(
    detail::feature_rows_are_one_per_tag(),
    "a feature tag needs exactly one row, and one status field"
);

struct FeatureSet {
    TASTY_SEAT_EXEMPT(component);
    std::array<bool, kFeatureRows.size()> named{};

    [[nodiscard]] constexpr bool has(Tag t) const noexcept {
        const FeatureRow* row = feature_row(t);
        return row && named[static_cast<std::size_t>(row - kFeatureRows.data())];
    }

    constexpr void add(Tag t) noexcept {
        if (const FeatureRow* row = feature_row(t))
            named[static_cast<std::size_t>(row - kFeatureRows.data())] = true;
    }

    friend constexpr bool operator==(const FeatureSet&, const FeatureSet&) = default;
};

struct UnknownTagCount {
    TASTY_SEAT_EXEMPT(component);
    std::uint16_t v = 0;

    constexpr void add() noexcept { ++v; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    friend constexpr bool operator==(UnknownTagCount, UnknownTagCount) = default;
};

struct DbTags {
    MemoryType save_type = MemoryType::None;
    SystemType system = SystemType::Unknown;
    Cic cic = Cic::Unknown;
    PadType preferred_pad = PadType::Pad;
    FeatureSet features{};
    UnknownTagCount unknown_tags{};
    PatchTable patches{};

    [[nodiscard]] constexpr bool complete() const noexcept {
        return system != SystemType::Unknown && cic != Cic::Unknown;
    }
};

namespace detail {

[[nodiscard]] constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

constexpr void prefer_pad(DbTags& out, PadType p) noexcept {
    if (out.preferred_pad == PadType::Pad)
        out.preferred_pad = p;
}

constexpr void apply_tag(DbTags& out, Tag t) noexcept {

    if (const FeatureRow* row = feature_row(t)) {
        out.features.add(t);
        if (const auto pad = row->pad)
            prefer_pad(out, *pad);
        return;
    }
    switch (t) {
        case Tag::Eeprom512:
            out.save_type = MemoryType::Eeprom512;
            break;
        case Tag::Eeprom2k:
            out.save_type = MemoryType::Eeprom2k;
            break;
        case Tag::Sram32k:
            out.save_type = MemoryType::Sram32k;
            break;
        case Tag::Sram96k:
            out.save_type = MemoryType::Sram96k;
            break;
        case Tag::Flash128k:
            out.save_type = MemoryType::Flash128k;
            break;

        case Tag::NoEpak:
        case Tag::Cpak:
        case Tag::Rpak:
        case Tag::Tpak:
        case Tag::Rtc:
            break;
        case Tag::Ntsc:
            out.system = SystemType::Ntsc;
            break;
        case Tag::Pal:
            out.system = SystemType::Pal;
            break;
        case Tag::Cic6101:
            out.cic = Cic::Nus6101;
            out.system = SystemType::Ntsc;
            break;
        case Tag::Cic6102:
            out.cic = Cic::Nus6102;
            out.system = SystemType::Ntsc;
            break;
        case Tag::Cic6103:
            out.cic = Cic::Nus6103;
            out.system = SystemType::Ntsc;
            break;
        case Tag::Cic6105:
            out.cic = Cic::Nus6105;
            out.system = SystemType::Ntsc;
            break;
        case Tag::Cic6106:
            out.cic = Cic::Nus6106;
            out.system = SystemType::Ntsc;
            break;
        case Tag::Cic7101:
            out.cic = Cic::Nus7101;
            out.system = SystemType::Pal;
            break;
        case Tag::Cic7102:
            out.cic = Cic::Nus7102;
            out.system = SystemType::Pal;
            break;
        case Tag::Cic7103:
            out.cic = Cic::Nus7103;
            out.system = SystemType::Pal;
            break;
        case Tag::Cic7105:
            out.cic = Cic::Nus7105;
            out.system = SystemType::Pal;
            break;
        case Tag::Cic7106:
            out.cic = Cic::Nus7106;
            out.system = SystemType::Pal;
            break;
        case Tag::Cic8303:
            out.cic = Cic::Nus8303;
            break;
        case Tag::Cic8401:
            out.cic = Cic::Nus8401;
            break;
        case Tag::Cic5167:
            out.cic = Cic::Nus5167;
            break;
        case Tag::CicDdus:
            out.cic = Cic::NusDdus;
            break;
        case Tag::Cic5101:
            out.cic = Cic::Nus5101;
            break;
        case Tag::Unknown:
            out.unknown_tags.add();
            break;
    }
}

}

[[nodiscard]] constexpr Tag tag_of(std::string_view token) noexcept {
    for (std::size_t i = 0; i < kTagCount; ++i)
        if (infra::ieq(token, kTagNames[i]))
            return static_cast<Tag>(i);
    return Tag::Unknown;
}

[[nodiscard]] constexpr std::optional<PatchTable::Entry> parse_patch_tag(
    std::string_view tag
) noexcept {
    std::size_t i = 0;
    const std::size_t n = tag.size();

    auto scan_hex = [&]() -> std::optional<std::uint32_t> {
        while (i < n && infra::ascii_space(tag[i]))
            ++i;
        std::size_t width = 0;
        bool negate = false;
        if (i < n && (tag[i] == '+' || tag[i] == '-')) {
            negate = tag[i] == '-';
            ++i;
            ++width;
        }
        if (width + 2 <= 8
            && i + 2 < n
            && tag[i] == '0'
            && (tag[i + 1] == 'x' || tag[i + 1] == 'X')
            && detail::hex_value(tag[i + 2]) >= 0) {
            i += 2;
            width += 2;
        }
        std::uint32_t v = 0;
        std::size_t digits = 0;
        while (i < n && width < 8 && detail::hex_value(tag[i]) >= 0) {
            v = v * 16u + static_cast<std::uint32_t>(detail::hex_value(tag[i]));
            ++i;
            ++width;
            ++digits;
        }
        if (digits == 0)
            return std::nullopt;
        return negate ? static_cast<std::uint32_t>(0u - v) : v;
    };

    while (i < n && (tag[i] == 'P' || tag[i] == 'p'))
        ++i;
    if (i == 0)
        return std::nullopt;

    const auto address = scan_hex();
    if (!address)
        return std::nullopt;
    if (i >= n || tag[i] != ':')
        return std::nullopt;
    ++i;
    const auto diff = scan_hex();
    if (!diff)
        return std::nullopt;

    PatchTable::Entry e{*address, *diff, false};
    std::size_t colons = 0;
    while (i < n && tag[i] == ':') {
        ++i;
        ++colons;
    }
    if (colons != 0 && i < n)
        e.turbo_core_only = tag[i] == 'T' || tag[i] == 't';
    return e;
}

[[nodiscard]] constexpr DbTags parse_tags(std::string_view tags) noexcept {
    DbTags out;
    std::size_t pos = 0;
    while (pos < tags.size()) {
        while (pos < tags.size() && kTagSeparators.find(tags[pos]) != std::string_view::npos)
            ++pos;
        const std::size_t start = pos;
        while (pos < tags.size() && kTagSeparators.find(tags[pos]) == std::string_view::npos)
            ++pos;
        if (start == pos)
            break;
        const std::string_view token = tags.substr(start, pos - start);

        if (const auto patch = parse_patch_tag(token)) {
            (void)out.patches.push(*patch);
            continue;
        }
        detail::apply_tag(out, tag_of(token));
    }

    if (out.system == SystemType::Unknown && out.cic != Cic::Unknown)
        out.system = SystemType::Ntsc;
    return out;
}

}
