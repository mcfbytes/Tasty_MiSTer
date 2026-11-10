// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_lsmv_codec.h"

#include "infra/parse_int.h"

namespace mister::cores {
namespace {

enum class SnesLsmvLayout : std::uint16_t {
    Defaulted = SnesLsmvCodec::kDefaulted,
    HardReset = SnesLsmvCodec::kHardReset,
    Compact = SnesLsmvCodec::kCompact,
    Gametype = SnesLsmvCodec::kGametype,
    RandomInit = SnesLsmvCodec::kRandomInit,
};

bool delay_number(std::string_view s) {
    if (s.starts_with('-'))
        s.remove_prefix(1);
    return s.size() <= 5 && infra::parse_int<int>(s).has_value();
}

void note_recorder_ram(MovieFacts& f) noexcept {
    if (f.layout.has(SnesLsmvLayout::RandomInit)) {
        f.recorder_ram = {};
        f.recorder_ram_fill.reset();
        return;
    }
    f.recorder_ram = {
        .kind = RamImageRecipe::Kind::Flat,
        .first_fill = 0x55,
        .second_fill = 0x00,
        .first_bytes = SnesMovieCodec::kWramBytes,
        .second_bytes = SnesMovieCodec::kAramBytes
    };
    f.recorder_ram_fill = SnesMovieCodec::initial_wram_value(SnesMovieCodec::WramFill::Flat55);
}

}

bool SnesLsmvCodec::starts_log(std::string_view line) const noexcept {

    return line.size() >= 2
        && (line[0] == 'F' || line[0] == '.')
        && (line[1] == '.' || line[1] == 'R');
}

Ex<MovieFacts> SnesLsmvCodec::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    MovieFacts f = so_far;
    if (!f.layout.has(SnesLsmvLayout::Defaulted)) {
        f.layout = f.layout.with(SnesLsmvLayout::Defaulted);
        f.ports = f.ports.with(svc::kPlayers[0]);
    }
    const auto [key, val] = split(line);
    if (key.empty())
        return f;

    if (key == "savestate"
        || key == "@savestate"
        || key.starts_with("savestate.")
        || key.starts_with("@savestate.")
        || key.starts_with("@moviesram.")
        || (key.starts_with("moviesram.") && !val.empty())
        || key.starts_with("initram.")
        || key.starts_with("@initram.")) {
        return std::unexpected(movie_refusal(MovieRefusal::Savestate, ERR_SITE()));
    }
    if (key == "systemid") {
        if (val != "lsnes-rr1")
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        f.version = MovieRevision{1};
    } else if (key == "controlsversion") {
        if (val != "0")
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    } else if (key == "gametype") {
        if (val == "snes_ntsc") {
            f.region = MovieRegion::Ntsc;
        } else if (val == "snes_pal") {
            f.region = MovieRegion::Pal;
        } else {
            return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
        }
        f.layout = f.layout.with(SnesLsmvLayout::Gametype);
    } else if (key == "port1" || key == "port2") {
        const svc::PlayerIndex slot = svc::kPlayers[key == "port1" ? 0 : 1];
        if (val == "gamepad") {
            f.ports = f.ports.with(slot);
        } else if (val == "none") {
            f.ports = ports_without(f.ports, slot);
        } else if (val.starts_with("multitap")) {
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
        } else {
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
        }
    } else if (
        key == "setting.hardreset" || key == "setting.compact" || key == "setting.radominit"
    ) {
        if (val != "0" && val != "1")
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        const SnesLsmvLayout bit = key == "setting.hardreset" ? SnesLsmvLayout::HardReset
            : key == "setting.compact"                        ? SnesLsmvLayout::Compact
                                                              : SnesLsmvLayout::RandomInit;
        f.layout = f.layout.with(bit, val == "1");
    } else if (key == "rom.sha256") {
        const auto d = parse_hex_digest(val, DigestKind::Sha256);
        if (!d)
            return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
        f.digest = *d;
    } else if (
        (key.starts_with("slot") && key.size() > 4 && !val.empty()) || key.starts_with("@slot")
    ) {

        return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
    }
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    note_recorder_ram(f);
    return f;
}

Ex<void> SnesLsmvCodec::finish_header(const MovieFacts& f) const noexcept {
    if (f.version != MovieRevision{1} || !f.layout.has(SnesLsmvLayout::Gametype))
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    if (f.ports.empty())
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    if (!f.digest)
        return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> SnesLsmvCodec::frame(std::string_view line, const MovieFacts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kMovieLineMax || !starts_log(line))
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));

    if (line[0] != 'F')
        return std::unexpected(movie_refusal(MovieRefusal::Subframe, ERR_SITE()));
    const std::size_t bar = line.find('|');
    std::string_view sys = line.substr(0, bar);
    std::string_view rest = bar == std::string_view::npos ? std::string_view{} : line.substr(bar);
    MovieFrame out{};
    if (sys[1] == 'R')
        out.commands = out.commands.with_reset();
    sys.remove_prefix(2);
    if (f.layout.has(SnesLsmvLayout::HardReset) || f.layout.has(SnesLsmvLayout::Compact)) {
        if (sys.empty() || (sys[0] != '.' && sys[0] != 'H'))
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        if (sys[0] == 'H')
            out.commands = out.commands.with_power();
        sys.remove_prefix(1);
    }
    if (!f.layout.has(SnesLsmvLayout::Compact)) {

        for (int n = 0; n < 2; ++n) {
            if (sys.empty() || (sys[0] != ' ' && sys[0] != '\t'))
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            while (!sys.empty() && (sys[0] == ' ' || sys[0] == '\t'))
                sys.remove_prefix(1);
            const std::size_t end = sys.find_first_of(" \t");
            if (!delay_number(sys.substr(0, end)))
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            sys.remove_prefix(end == std::string_view::npos ? sys.size() : end);
        }
    }
    if (!sys.empty())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    for (std::size_t p = 0; p < 2; ++p) {
        if (!f.ports.has(svc::kPlayers[p]))
            continue;
        if (rest.size() < 1 + kPadOrder.size() || rest[0] != '|')
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        proto::JoyMask m{};
        for (std::size_t i = 0; i < kPadOrder.size(); ++i) {
            const char c = rest[1 + i];
            if (c <= 0x20 || c > 0x7E || c == '|')
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            if (c != '.')
                m = m.with(SnesMovieCodec::bit(kPadOrder[i]));
        }
        out.mask[p] = m;
        rest.remove_prefix(1 + kPadOrder.size());
    }
    if (!rest.empty())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
}

std::optional<IMovieFormat::DigestSpan> SnesLsmvCodec::rom_digest_span(
    std::span<const std::uint8_t>,
    proto::FileSize size
) const noexcept {

    return SnesMovieCodec::past_header(size, tail_remainder(size, kCopierGrid));
}

}
