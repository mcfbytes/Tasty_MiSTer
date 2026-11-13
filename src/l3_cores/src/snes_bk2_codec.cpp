// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_bk2_codec.h"

#include "cores/bk2_log.h"
#include "cores/bk2_text.h"

namespace mister::cores {
namespace {

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
    return bk2_starts_log(line);
}

bool SnesBk2Codec::ends_log(std::string_view line) const noexcept {
    return bk2_ends_log(line);
}

Ex<Bk2Column> SnesBk2Codec::column_of(std::string_view name) noexcept {
    if (name == "Reset")
        return Bk2Column::reset();
    if (name == "Power")
        return Bk2Column::power();
    if (name == "Subframe")
        return Bk2Column::subframe();
    if (name == "Reset Instruction")
        return Bk2Column::reset_delay();

    const auto pad = bk2_pad_name(name);
    if (!pad)
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    constexpr auto kLastPort = proto::PlayerIndex::of(1u);
    for (const PadName& p : kPadNames) {
        if (pad->name != p.name)
            continue;
        if (pad->port > kLastPort)
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        const auto c = Bk2Column::pad(pad->port, SnesMovieCodec::joy_bit(p.button));
        if (!c)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
        return *c;
    }
    return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
}

Ex<MovieFacts> SnesBk2Codec::header_line(
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
            const auto folded = bk2_fold_groups<SnesBk2Codec>(l.value, f);
            if (!folded)
                return std::unexpected(folded.error());
            f = *folded;
            f.layout = f.layout.with(Bk2Layout::LogKey);
            f.recorder_ram = recorder_ram(f);
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
        f.region = *b ? MovieRegion::Pal : MovieRegion::Ntsc;
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
        const auto b = bk2_boolean(val);
        if (!b)
            return std::unexpected(movie_refusal(MovieRefusal::Setting, ERR_SITE()));
        if (!*b)
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
    return bk2_decode_grouped(line, f);
}

std::optional<IMovieFormat::DigestSpan> SnesBk2Codec::rom_digest_span(
    std::span<const std::uint8_t>,
    proto::FileSize size
) const noexcept {
    return SnesMovieCodec::past_header(size, copier_header(size));
}

}
