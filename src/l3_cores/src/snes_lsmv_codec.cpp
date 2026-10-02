// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_lsmv_codec.h"

#include <charconv>
#include <system_error>

namespace mister::cores {
namespace {

bool delay_number(std::string_view s) {
    if (!s.empty() && s.front() == '-') s.remove_prefix(1);
    if (s.empty() || s.size() > 5) return false;
    int v = 0;
    const auto [at, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} && at == s.data() + s.size();
}

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

}  // namespace

bool SnesLsmvCodec::starts_log(std::string_view line) const noexcept {

    return line.size() >= 2 && (line[0] == 'F' || line[0] == '.') &&
           (line[1] == '.' || line[1] == 'R');
}

Ex<IMovieCodec::Facts> SnesLsmvCodec::header_line(std::string_view line,
                                                  const Facts& so_far) const noexcept {
    Facts f = so_far;
    if ((f.layout & kDefaulted) == 0) {
        f.layout |= kDefaulted;
        f.ports |= 0x1u;
    }
    const auto [key, val] = split(line);
    if (key.empty()) return f;

    if (key == "savestate" || key == "@savestate" || starts_with(key, "savestate.") ||
        starts_with(key, "@savestate.") || starts_with(key, "@moviesram.") ||
        (starts_with(key, "moviesram.") && !val.empty()) || starts_with(key, "initram.") ||
        starts_with(key, "@initram.")) {
        return std::unexpected(refuse(Refusal::Savestate, ERR_SITE()));
    }
    if (key == "systemid") {
        if (val != "lsnes-rr1") return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        f.version = 1;
    } else if (key == "controlsversion") {
        if (val != "0") return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
    } else if (key == "gametype") {
        if (val == "snes_ntsc") {
            f.region = Region::Ntsc;
        } else if (val == "snes_pal") {
            f.region = Region::Pal;
        } else {
            return std::unexpected(refuse(Refusal::System, ERR_SITE()));
        }
        f.layout |= kGametype;
    } else if (key == "port1" || key == "port2") {
        const std::uint8_t bit = key == "port1" ? 0x1u : 0x2u;
        if (val == "gamepad") {
            f.ports |= bit;
        } else if (val == "none") {
            f.ports &= static_cast<std::uint8_t>(~bit);
        } else if (starts_with(val, "multitap")) {
            return std::unexpected(refuse(Refusal::Multitap, ERR_SITE()));
        } else {
            return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
        }
    } else if (key == "setting.hardreset" || key == "setting.compact" ||
               key == "setting.radominit") {
        if (val != "0" && val != "1")
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        const std::uint8_t bit = key == "setting.hardreset" ? kHardReset
                                 : key == "setting.compact" ? kCompact
                                                            : kRandomInit;
        f.layout = static_cast<std::uint16_t>(val == "1" ? (f.layout | bit) : (f.layout & ~bit));
    } else if (key == "rom.sha256") {
        const auto d = parse_hex_digest(val, DigestKind::Sha256);
        if (!d) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
        f.digest = *d;
        f.has_digest = true;
    } else if ((starts_with(key, "slot") && key.size() > 4 && !val.empty()) ||
               starts_with(key, "@slot")) {

        return std::unexpected(refuse(Refusal::System, ERR_SITE()));
    }
    note_rerecords(key, val, f);
    return f;
}

std::optional<SnesMovieCodec::WramFill> SnesLsmvCodec::do_recorder_wram(
    const Facts& f) const noexcept {
    if ((f.layout & kRandomInit) != 0) return std::nullopt;
    return WramFill::Flat55;
}

RamImageRecipe SnesLsmvCodec::do_recorder_ram(const Facts& f) const noexcept {
    if ((f.layout & kRandomInit) != 0) return {};
    return {.kind = RamImageRecipe::Kind::Flat,
            .first_fill = 0x55,
            .second_fill = 0x00,
            .first_bytes = kWramBytes,
            .second_bytes = kAramBytes};
}

Ex<void> SnesLsmvCodec::finish_header(const Facts& f) const noexcept {
    if (f.version != 1 || (f.layout & kGametype) == 0)
        return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
    if (f.ports == 0) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    if (!f.has_digest) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> SnesLsmvCodec::frame(std::string_view line, const Facts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kLineMax || !starts_log(line))
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));

    if (line[0] != 'F') return std::unexpected(refuse(Refusal::Subframe, ERR_SITE()));
    const std::size_t bar = line.find('|');
    std::string_view sys = line.substr(0, bar);
    std::string_view rest = bar == std::string_view::npos ? std::string_view{} : line.substr(bar);
    Frame out{};
    if (sys[1] == 'R') out.commands |= 0x1u;
    sys.remove_prefix(2);
    if ((f.layout & (kHardReset | kCompact)) != 0) {
        if (sys.empty() || (sys[0] != '.' && sys[0] != 'H'))
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        if (sys[0] == 'H') out.commands |= 0x2u;
        sys.remove_prefix(1);
    }
    if ((f.layout & kCompact) == 0) {

        for (int n = 0; n < 2; ++n) {
            if (sys.empty() || (sys[0] != ' ' && sys[0] != '\t'))
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            while (!sys.empty() && (sys[0] == ' ' || sys[0] == '\t'))
                sys.remove_prefix(1);
            const std::size_t end = sys.find_first_of(" \t");
            if (!delay_number(sys.substr(0, end)))
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            sys.remove_prefix(end == std::string_view::npos ? sys.size() : end);
        }
    }
    if (!sys.empty()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    for (std::size_t p = 0; p < 2; ++p) {
        if ((f.ports & (1u << p)) == 0) continue;
        if (rest.size() < 1 + kPadOrder.size() || rest[0] != '|')
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < kPadOrder.size(); ++i) {
            const char c = rest[1 + i];
            if (c <= 0x20 || c > 0x7E || c == '|')
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            if (c != '.') m |= bit(kPadOrder[i]);
        }
        out.mask[p] = m;
        rest.remove_prefix(1 + kPadOrder.size());
    }
    if (!rest.empty()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    return out;
}

std::optional<IMovieCodec::DigestSpan> SnesLsmvCodec::rom_digest_span(
    std::span<const std::uint8_t>, std::uint64_t size) const noexcept {
    return past_header(size, size % 1024u);
}

}  // namespace mister::cores
