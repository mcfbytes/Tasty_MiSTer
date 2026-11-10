// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/chd_track.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "disc_lex.h"
#include "infra/fixed_str.h"

namespace mister::svc {

namespace {

using namespace disc::detail;

struct ChdTypeRow {
    std::string_view name;
    ChdSectorSize sector_size;
    TrackType type;
};

constexpr ChdTypeRow kChdTypes[] = {
    {"MODE1_RAW", ChdSectorSize::Raw, TrackType::Mode1},
    {"MODE2_RAW", ChdSectorSize::Raw, TrackType::Mode2},
    {"MODE1", ChdSectorSize::UserData, TrackType::Mode1},
    {"MODE2", ChdSectorSize::Mode2Cooked, TrackType::Mode2},
    {"AUDIO", ChdSectorSize::Raw, TrackType::Cdda},
};

struct ChdTypeShape {
    TrackType type;
    ChdSectorSize sector_size;
};

[[nodiscard]] ChdTypeShape decode_chd_type(std::string_view name) noexcept {
    for (const ChdTypeRow& row : kChdTypes)
        if (row.name == name)
            return ChdTypeShape{row.type, row.sector_size};
    return ChdTypeShape{TrackType::Cdda, ChdSectorSize::None};
}

[[nodiscard]] ChdSubcode decode_chd_subcode(std::string_view name) noexcept {
    if (name == "RW")
        return ChdSubcode::Rw;
    if (name == "RW_RAW")
        return ChdSubcode::RwRaw;
    return ChdSubcode::None;
}

bool take_key(std::string_view& s, std::string_view key) noexcept {
    s = lstrip(s);
    if (!s.starts_with(key))
        return false;
    s.remove_prefix(key.size());
    return true;
}

[[nodiscard]] bool take_token(std::string_view& s, FixedStr<32, StrFit::Clip>& out) noexcept {
    s = lstrip(s);
    const std::size_t end = s.find_first_of(" \t");
    const std::string_view token = s.substr(0, end);
    const bool stored = out.assign(token);
    s.remove_prefix(token.size());
    return stored && !token.empty() && !out.clipped();
}

}

std::optional<ChdTrack> parse_chd_track(const ChdMetaRow& row) noexcept {
    std::string_view s = row.text;
    ChdTrack out{};
    FixedStr<32, StrFit::Clip> type_text;
    FixedStr<32, StrFit::Clip> subtype_text;
    FixedStr<32, StrFit::Clip> pgtype_text;
    FixedStr<32, StrFit::Clip> pgsub_text;
    if (!take_key(s, "TRACK:"))
        return std::nullopt;
    const auto number = take_uint(s);
    if (!number)
        return std::nullopt;

    out.number = TrackNumber::of(*number);
    if (!take_key(s, "TYPE:") || !take_token(s, type_text))
        return std::nullopt;
    const ChdTypeShape shape = decode_chd_type(type_text.view());
    out.type = shape.type;
    out.sector_size = shape.sector_size;
    if (!take_key(s, "SUBTYPE:") || !take_token(s, subtype_text))
        return std::nullopt;
    out.subcode = decode_chd_subcode(subtype_text.view());
    if (!take_key(s, "FRAMES:"))
        return std::nullopt;
    const auto frames = take_uint(s);
    if (!frames)
        return std::nullopt;
    out.frames = BlockCount{*frames};

    if (!row.v2)
        return out;
    if (!take_key(s, "PREGAP:"))
        return std::nullopt;
    const auto pregap = take_uint(s);
    if (!pregap)
        return std::nullopt;
    out.pregap = BlockCount{*pregap};
    if (!take_key(s, "PGTYPE:") || !take_token(s, pgtype_text))
        return std::nullopt;
    out.pregap_held = pgtype_text.view().starts_with('V');
    if (!take_key(s, "PGSUB:") || !take_token(s, pgsub_text))
        return std::nullopt;
    if (!take_key(s, "POSTGAP:"))
        return std::nullopt;
    const auto postgap = take_uint(s);
    if (!postgap)
        return std::nullopt;
    out.postgap = BlockCount{*postgap};
    return out;
}

void ChdTrack::apply_type_to(Track& track) const noexcept {
    track.type = type;
    track.sector_size = detail::chd_sector_bytes(sector_size);
}

}
