// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nes_bk2_codec.h"

#include "cores/bk2_log.h"
#include "cores/bk2_text.h"
#include "cores/digest_value.h"
#include "svc/file.h"
#include "svc/vfs.h"

#include <array>
#include <span>

#include <string_view>

namespace mister::cores {
namespace {

bool is_pal_region(std::string_view v) {
    return v == "2" || v == "PAL" || v == "pal";
}

bool is_dendy_region(std::string_view v) {
    return v == "3" || v == "Dendy" || v == "dendy";
}

std::array<std::uint8_t, IMovieFormat::kRomHead> ines1_header(std::span<const std::uint8_t> h) {
    std::array<std::uint8_t, IMovieFormat::kRomHead> out{};
    for (std::size_t i = 0; i < 7; ++i)
        out[i] = h[i];

    const unsigned id = h[7] & 0x0Cu;
    const bool archaic = id == 0x04u
        || id == 0x0Cu
        || (id == 0 && (h[12] | h[13] | h[14] | h[15]) != 0);
    out[7] = archaic ? 0 : static_cast<std::uint8_t>(h[7] & 0xF3u);
    return out;
}

bool is_ines(std::span<const std::uint8_t> h) {
    return h.size() >= IMovieFormat::kRomHead
        && h[0] == 'N'
        && h[1] == 'E'
        && h[2] == 'S'
        && h[3] == 0x1A;
}

}

bool NesBk2Codec::starts_log(std::string_view line) const noexcept {
    return bk2_starts_log(line);
}

bool NesBk2Codec::ends_log(std::string_view line) const noexcept {
    return bk2_ends_log(line);
}

Ex<Bk2Column> NesBk2Codec::column_of(std::string_view name) noexcept {
    if (name == "Reset")
        return Bk2Column::reset();
    if (name == "Power")
        return Bk2Column::power();
    if (name == "Reset Cycle")
        return std::unexpected(movie_refusal(MovieRefusal::Subframe, ERR_SITE()));
    if (name.starts_with("Insert Coin") || name == "Service Switch")
        return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
    if (name.find("FDS") != std::string_view::npos)
        return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));

    const auto pad = bk2_pad_name(name);
    if (!pad)
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    constexpr auto kLastPort = proto::PlayerIndex::of(1u);
    for (const PadName& p : kPadNames) {
        if (pad->name != p.name)
            continue;
        if (pad->port > kLastPort)
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        const auto c = Bk2Column::pad(pad->port, p.bit);
        if (!c)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
        return *c;
    }
    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
}

Ex<MovieFacts> NesBk2Codec::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    MovieFacts f = so_far;
    const auto read = bk2_read_line(line);
    if (!read)
        return std::unexpected(read.error());
    const Bk2Line& l = *read;
    switch (l.kind) {
        case Bk2Line::Kind::Blank:
        case Bk2Line::Kind::Long:
            return f;
        case Bk2Line::Kind::LogKey: {
            if (f.layout.has(Bk2Layout::LogKey))
                return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
            const auto folded = bk2_fold_groups<NesBk2Codec>(l.value, f);
            if (!folded)
                return std::unexpected(folded.error());
            f = *folded;
            f.layout = f.layout.with(Bk2Layout::LogKey);
            return f;
        }
        case Bk2Line::Kind::Pair:
            break;
    }
    const std::string_view key = l.key;
    const std::string_view val = l.value;
    if (key == "MovieVersion") {
        if (!bk2_version_ok(val))
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        f.layout = f.layout.with(Bk2Layout::MovieVersion);
    } else if (key == "Platform") {
        if (val != kPlatformName)
            return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
        f.layout = f.layout.with(Bk2Layout::Platform);
    } else if (key == "PAL") {
        const auto b = bk2_boolean(val);
        if (!b)
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        if (*b)
            f.region = MovieRegion::Pal;
    } else if (key == "SyncSettings.o.RegionOverride" || key == "SyncSettings.RegionOverride") {
        if (is_dendy_region(val))
            return std::unexpected(movie_refusal(MovieRefusal::Setting, ERR_SITE()));
        if (is_pal_region(val))
            f.region = MovieRegion::Pal;
    } else if (key == "SyncSettings.o.Controls.Famicom" || key == "SyncSettings.Controls.Famicom") {
        const auto b = bk2_boolean(val);
        if (b && *b)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (
        key == "SyncSettings.o.Controls.NesLeftPort" || key == "SyncSettings.Controls.NesLeftPort"
    ) {
        if (val == "FourScore")
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        if (val != "ControllerNES")
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (
        key == "SyncSettings.o.Controls.NesRightPort" || key == "SyncSettings.Controls.NesRightPort"
    ) {
        if (val == "FourScore")
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        if (val != "ControllerNES" && val != "UnpluggedNES")
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (
        key == "SyncSettings.o.Controls.FamicomExpPort"
        || key == "SyncSettings.Controls.FamicomExpPort"
    ) {
        if (val == "Famicom4P")
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        if (val != "UnpluggedFam")
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (key == "BoardName" && (val == "FDS" || val.starts_with("FDS"))) {
        return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));
    } else if (key == "IsFDS") {
        const auto b = bk2_boolean(val);
        if (b && *b)
            return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));
    } else {
        const auto d = bk2_game_db_digest(key, val, f.layout.has(Bk2Layout::Sha1));
        if (!d)
            return std::unexpected(d.error());
        if (*d)
            f.digest = **d;
        if (key == "SHA1")
            f.layout = f.layout.with(Bk2Layout::Sha1);
    }
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    return f;
}

Ex<void> NesBk2Codec::finish_header(const MovieFacts& f) const noexcept {
    if (!f.layout.has_all(Bk2Layout::MovieVersion, Bk2Layout::Platform, Bk2Layout::LogKey))
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    if (f.ports.empty())
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    if (!f.digest)
        return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> NesBk2Codec::frame(std::string_view line, const MovieFacts& f) const noexcept {
    return bk2_decode_grouped(line, f);
}

std::optional<IMovieFormat::DigestSpan> NesBk2Codec::rom_digest_span(
    std::span<const std::uint8_t> head,
    proto::FileSize size
) const noexcept {
    if (is_ines(head) && size <= proto::FileSize{kRomHead})
        return std::nullopt;

    const proto::FileSize skip = copier_header(size);
    if (size <= skip)
        return std::nullopt;
    return span_past(size, skip);
}

Ex<IMovieFormat::DigestSource> NesBk2Codec::digest_source(
    const svc::Vfs& vfs,
    std::string_view rom
) const {
    auto src = head_digest_source(vfs, rom);
    if (!src)
        return src;
    auto f = vfs.open(rom, svc::OpenMode::Read);
    if (!f)
        return std::unexpected(f.error());
    std::array<std::uint8_t, kRomHead> head{};
    const auto got = (*f)->read_at(0, std::as_writable_bytes(std::span<std::uint8_t>(head)));
    if (!got)
        return std::unexpected(got.error());
    if (src->span.offset != proto::FileSize{} || *got != kRomHead || !is_ines(head))
        return src;
    const auto canon = ines1_header(head);
    src->span = DigestSpan{
        .offset = proto::FileSize{kRomHead},
        .length = src->span.length - proto::FileSize{kRomHead}
    };
    src->prefix.assign(head.begin(), head.end());
    if (canon != head)
        src->alt_prefixes.emplace_back(canon.begin(), canon.end());
    src->alt_prefixes.emplace_back();
    return src;
}

}
