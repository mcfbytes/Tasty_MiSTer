// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_bk2_codec.h"

#include "infra/indexed_array.h"
#include "infra/parse_int.h"

namespace mister::cores {
namespace {

bool truthy(std::string_view v) {
    return v == "1" || v == "True" || v == "true";
}

bool falsy(std::string_view v) {
    return v == "0" || v == "False" || v == "false";
}

constexpr std::string_view kLogKeyPrefix = "LogKey:";

RamImageRecipe recorder_ram(const MovieFacts& f) noexcept {

    if (f.layout.has(Bk2Layout::Libsnes)) {
        if (f.layout.has(Bk2Layout::LibsnesFlat))
            return {
                .kind = RamImageRecipe::Kind::Flat,
                .first_fill = 0x55,
                .second_fill = 0x00,
                .first_bytes = SnesMovieCodec::kWramBytes,
                .second_bytes = SnesMovieCodec::kAramBytes
            };
        return {
            .kind = RamImageRecipe::Kind::Lfsr,
            .seed = 0,
            .first_bytes = SnesMovieCodec::kWramBytes,
            .second_bytes = SnesMovieCodec::kAramBytes
        };
    }
    if (!f.layout.has(Bk2Layout::Bsnes115))
        return {};

    const bool low_bit = f.layout.has(Bk2Layout::EntropyBit0);
    const bool high_bit = f.layout.has(Bk2Layout::EntropyBit1);
    const auto entropy = !high_bit
        ? (low_bit ? RamImageRecipe::Entropy::None : RamImageRecipe::Entropy::Low)
        : (low_bit ? RamImageRecipe::Entropy::High : RamImageRecipe::Entropy::Low);
    return {
        .kind = RamImageRecipe::Kind::Pcg32,
        .entropy = entropy,
        .second_fill = 0x00,
        .seed = SnesBk2Codec::kSandboxClockSeed,
        .first_bytes = SnesMovieCodec::kWramBytes,
        .second_bytes = SnesMovieCodec::kAramBytes
    };
}

}

bool SnesBk2Codec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

bool SnesBk2Codec::ends_log(std::string_view line) const noexcept {
    return trim(line) == "[/Input]";
}

Ex<SnesBk2Codec::Bk2Column> SnesBk2Codec::column_of(std::string_view name) noexcept {
    if (name == "Reset")
        return Bk2Column{kColReset};
    if (name == "Power")
        return Bk2Column{kColPower};
    if (name == "Subframe")
        return Bk2Column{kColSubframe};
    if (name == "Reset Instruction")
        return Bk2Column{kColResetDelay};

    if (name.size() < 4 || name[0] != 'P' || name[1] < '1' || name[1] > '9' || name[2] != ' ')
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    const unsigned port = static_cast<unsigned>(name[1] - '1');
    for (const PadName& p : kPadNames) {
        if (name.substr(3) != p.name)
            continue;
        if (port > 1)
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        return Bk2Column::pad(svc::PlayerIndex::of(port), p.button);
    }
    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
}

Ex<MovieFacts> SnesBk2Codec::header_line(
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
                if (!c->append_to(f))
                    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
                if (c->is_pad())
                    ports = ports.with(c->port());
            }
            if (!Bk2Column{kColGroupEnd}.append_to(f))
                return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
            if (hash == std::string_view::npos)
                break;
            key.remove_prefix(hash + 1);
        }
        f.ports = ports;
        f.layout = f.layout.with(Bk2Layout::LogKey);
        f.recorder_ram = recorder_ram(f);
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
        f.region = truthy(val) ? MovieRegion::Pal : MovieRegion::Ntsc;
    } else if (key == "SyncSettings.o.LeftPort" || key == "SyncSettings.LeftPort") {

        if (val == "0" || val == "None" || val == "Unplugged")
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (key == "Core") {
        if (val == "BSNESv115+" || val == "SubBSNESv115+")
            f.layout = f.layout.with(Bk2Layout::Bsnes115);
        if (val == "BSNES")
            f.layout = f.layout.with(Bk2Layout::Libsnes);
    } else if (
        key == "SyncSettings.o.RandomizedInitialState"
        || key == "SyncSettings.RandomizedInitialState"
    ) {
        if (!truthy(val) && !falsy(val))
            return std::unexpected(movie_refusal(MovieRefusal::Setting, ERR_SITE()));
        if (falsy(val))
            f.layout = f.layout.with(Bk2Layout::LibsnesFlat);
    } else if (key == "SyncSettings.o.Entropy" || key == "SyncSettings.Entropy") {
        const int e = val == "0" || val == "None" ? 0
            : val == "1" || val == "Low"          ? 1
            : val == "2" || val == "High"         ? 2
                                                  : -1;
        if (e < 0)
            return std::unexpected(movie_refusal(MovieRefusal::Setting, ERR_SITE()));

        f.layout = f.layout.with(Bk2Layout::EntropyBit0, (e & 1) == 0)
                       .with(Bk2Layout::EntropyBit1, e >= 1);
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
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    f.recorder_ram = recorder_ram(f);
    return f;
}

Ex<void> SnesBk2Codec::finish_header(const MovieFacts& f) const noexcept {
    if (!f.layout.has_all(Bk2Layout::MovieVersion, Bk2Layout::Platform, Bk2Layout::LogKey))
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    if (f.ports.empty())
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    if (!f.digest)
        return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> SnesBk2Codec::frame(std::string_view line, const MovieFacts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kMovieLineMax || !starts_log(line))
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    MovieFrame out{};
    std::size_t at = 1;
    for (const std::size_t i : f.columns_n.indices()) {
        const Bk2Column c{f.columns[i]};
        if (at >= line.size())
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (c.is_group_end()) {
            if (line[at++] != '|')
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            continue;
        }
        if (c.is_reset_delay()) {

            const std::size_t comma = line.find(',', at);
            if (comma == std::string_view::npos || comma - at > 12)
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            std::string_view v = line.substr(at, comma - at);
            while (!v.empty() && v.front() == ' ')
                v.remove_prefix(1);
            if (!infra::parse_int<int>(v))
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            at = comma + 1;
            continue;
        }
        const char ch = line[at++];
        if (ch <= 0x20 || ch > 0x7E || ch == '|')
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (ch == '.')
            continue;
        if (c.is_reset()) {
            out.commands = out.commands.with_reset();
        } else if (c.is_power()) {
            out.commands = out.commands.with_power();
        } else if (c.is_subframe()) {
            return std::unexpected(movie_refusal(MovieRefusal::Subframe, ERR_SITE()));
        } else if (c.is_pad()) {
            proto::JoyMask& mask = infra::at_index(out.mask, c.port());
            mask = mask.with(c.bit().mask());
        }
    }
    if (at != line.size())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
}

std::optional<IMovieFormat::DigestSpan> SnesBk2Codec::rom_digest_span(
    std::span<const std::uint8_t>,
    proto::FileSize size
) const noexcept {
    return SnesMovieCodec::past_header(size, copier_header(size));
}

}
