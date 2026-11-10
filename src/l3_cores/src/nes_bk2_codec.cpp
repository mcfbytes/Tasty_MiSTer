// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nes_bk2_codec.h"

#include "cores/digest_value.h"
#include "svc/file.h"
#include "svc/vfs.h"

#include <array>
#include <span>

#include <string_view>

namespace mister::cores {
namespace {

bool truthy(std::string_view v) {
    return v == "1" || v == "True" || v == "true";
}

bool falsy(std::string_view v) {
    return v == "0" || v == "False" || v == "false";
}

constexpr std::string_view kLogKeyPrefix = "LogKey:";

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
    return !line.empty() && line.front() == '|';
}

bool NesBk2Codec::ends_log(std::string_view line) const noexcept {
    return trim(line) == "[/Input]";
}

Ex<std::uint8_t> NesBk2Codec::column_of(std::string_view name) noexcept {
    if (name == "Reset")
        return kColReset;
    if (name == "Power")
        return kColPower;
    if (name == "Reset Cycle")
        return std::unexpected(movie_refusal(MovieRefusal::Subframe, ERR_SITE()));
    if (name.starts_with("Insert Coin") || name == "Service Switch")
        return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
    if (name.find("FDS") != std::string_view::npos)
        return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));

    if (name.size() < 4 || name[0] != 'P' || name[1] < '1' || name[1] > '9' || name[2] != ' ')
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    const unsigned port = static_cast<unsigned>(name[1] - '1');
    for (const PadName& p : kPadNames) {
        if (name.substr(3) != p.name)
            continue;
        if (port > 1)
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        return static_cast<std::uint8_t>(kColPad | port << 4 | p.bit.v);
    }
    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
}

Ex<MovieFacts> NesBk2Codec::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    MovieFacts f = so_far;
    line = trim(line);
    if (line.starts_with(kLogKeyPrefix)) {
        if (f.layout.has(Bk2Layout::LogKey))
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        std::string_view key = line.substr(kLogKeyPrefix.size());
        const std::size_t first = key.find('#');
        if (first == std::string_view::npos)
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        key.remove_prefix(first + 1);
        std::size_t n = 0;
        svc::PlayerMask ports{};
        while (true) {
            const std::size_t hash = key.find('#');
            std::string_view group = key.substr(0, hash);
            while (!group.empty()) {
                const std::size_t bar = group.find('|');
                const std::string_view name = group.substr(0, bar);
                group.remove_prefix(bar == std::string_view::npos ? group.size() : bar + 1);
                if (name.empty())
                    continue;
                const auto c = column_of(name);
                if (!c)
                    return std::unexpected(c.error());
                if (n + 1 >= kMovieColumnsMax)
                    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
                f.columns[n++] = *c;
                if ((*c & kColPad) != 0)
                    ports = ports.with(svc::PlayerIndex::of((*c >> 4) & 0x7u));
            }
            if (n >= kMovieColumnsMax)
                return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
            f.columns[n++] = kColGroupEnd;
            if (hash == std::string_view::npos)
                break;
            key.remove_prefix(hash + 1);
        }
        f.columns_n = ColumnCount::of(n);
        f.ports = ports;
        f.layout = f.layout.with(Bk2Layout::LogKey);
        return f;
    }
    const auto [key, val] = split(line);
    if (key == "@Core"
        || key == "@CoreText"
        || key == "@SaveRam"
        || key == "SavestateBinaryBase64Blob"
        || ((key == "StartsFromSavestate" || key == "StartsFromSaveRam") && truthy(val))) {
        return std::unexpected(movie_refusal(MovieRefusal::Savestate, ERR_SITE()));
    }
    if (key == "MovieVersion") {
        if (!val.starts_with("BizHawk v2"))
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        f.layout = f.layout.with(Bk2Layout::MovieVersion);
    } else if (key == "Platform") {
        if (val != kPlatformName)
            return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
        f.layout = f.layout.with(Bk2Layout::Platform);
    } else if (key == "PAL") {
        if (!truthy(val) && !falsy(val))
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        if (truthy(val))
            f.region = MovieRegion::Pal;
    } else if (key == "SyncSettings.o.RegionOverride" || key == "SyncSettings.RegionOverride") {
        if (is_dendy_region(val))
            return std::unexpected(movie_refusal(MovieRefusal::Setting, ERR_SITE()));
        if (is_pal_region(val))
            f.region = MovieRegion::Pal;
    } else if (key == "SyncSettings.o.Controls.Famicom" || key == "SyncSettings.Controls.Famicom") {
        if (truthy(val))
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
    } else if (key == "IsFDS" && truthy(val)) {
        return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));
    } else if (
        key == "SHA1" || ((key == "SHA256" || key == "MD5") && !f.layout.has(Bk2Layout::Sha1))
    ) {
        const DigestKind kind = key == "SHA256" ? DigestKind::Sha256
            : key == "MD5" || val.size() == 32  ? DigestKind::Md5
            : val.size() == 8                   ? DigestKind::Crc32
                                                : DigestKind::Sha1;
        const auto d = parse_hex_digest(val, kind);
        if (!d)
            return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
        f.digest = *d;
        if (key == "SHA1")
            f.layout = f.layout.with(Bk2Layout::Sha1);
    }
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
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kMovieLineMax || !starts_log(line))
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    MovieFrame out{};
    std::size_t at = 1;
    for (const std::size_t i : f.columns_n.indices()) {
        const std::uint8_t c = f.columns[i];
        if (at >= line.size())
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (c == kColGroupEnd) {
            if (line[at++] != '|')
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            continue;
        }
        const char ch = line[at++];
        if (ch <= 0x20 || ch > 0x7E || ch == '|')
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (ch == '.')
            continue;
        if (c == kColReset) {
            out.commands = out.commands.with_reset();
        } else if (c == kColPower) {
            out.commands = out.commands.with_power();
        } else if ((c & kColPad) != 0) {
            proto::JoyMask& mask = out.mask[(c >> 4) & 0x7u];
            mask = mask.with(JoyBit{static_cast<std::uint8_t>(c & 0xFu)}.mask());
        }
    }
    if (at != line.size())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
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
