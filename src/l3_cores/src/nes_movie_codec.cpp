// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nes_movie_codec.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <system_error>

namespace mister::cores {
namespace {

Error refuse(IMovieCodec::Refusal r, std::uint16_t site) {
    return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
}

std::optional<std::uint32_t> decimal(std::string_view s, std::size_t digits) {
    if (s.empty() || s.size() > digits) return std::nullopt;
    std::uint32_t v = 0;
    const char* const end = s.data() + s.size();
    const auto [at, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || at != end) return std::nullopt;
    return v;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

int b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

struct SettingRule {
    std::uint8_t lo;
    std::uint8_t width;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr std::uint8_t kRamClearLo = 65;

constexpr std::array<SettingRule, 7> kRules{{
    {9, 1, 0x01, "Swap Joysticks"},
    {51, 2, 0x01, "SNAC"},
    {10, 1, 0x01, "Multitap"},
    {32, 3, 0x01, "Peripheral"},
    {53, 1, 0x01, "Famicom Keyboard"},
    {41, 1, 0x01, "Pause when OSD is open"},
    {kRamClearLo, 2, 0x06, "RAM Clear"},
}};

constexpr std::uint8_t kSysTypeLo = 23;

constexpr std::uint64_t kRamClearClocks = 0xF'FFFFull * 16u + 259u;

constexpr std::uint32_t kClkNtscHz = 21'477'272;
constexpr std::uint32_t kClkPalHz = 21'281'370;

}  // namespace

bool NesMovieCodec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

std::optional<std::array<std::uint8_t, 16>> NesMovieCodec::decode_digest(
    std::string_view text) noexcept {
    constexpr std::string_view kPrefix = "base64:";
    if (text.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
    text.remove_prefix(kPrefix.size());
    if (text.size() != 24 || text.substr(22) != "==") return std::nullopt;
    std::array<std::uint8_t, 16> out{};
    std::uint32_t acc = 0;
    unsigned bits = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < 22; ++i) {
        const int v = b64(text[i]);
        if (v < 0) return std::nullopt;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n < out.size()) out[n++] = static_cast<std::uint8_t>((acc >> bits) & 0xFFu);
        }
    }
    if (n != out.size()) return std::nullopt;
    return out;
}

Ex<IMovieCodec::Facts> NesMovieCodec::header_line(std::string_view line,
                                                  const Facts& so_far) const noexcept {
    Facts f = so_far;
    line = trim(line);
    if (line.empty()) return f;
    const std::size_t sp = line.find(' ');
    const std::string_view key = line.substr(0, sp);
    const std::string_view val =
        sp == std::string_view::npos ? std::string_view{} : trim(line.substr(sp + 1));

    if (key == "savestate") return std::unexpected(refuse(Refusal::Savestate, ERR_SITE()));
    if (line.size() > kLineMax) return f;
    const auto flag = [&]() -> Ex<bool> {
        const auto v = decimal(val, 1);
        if (!v || *v > 1) return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        return *v == 1;
    };
    if (key == "version") {
        const auto v = decimal(val, 3);
        if (!v || *v != 3) return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        f.version = static_cast<std::uint16_t>(*v);
    } else if (key == "palFlag") {
        const auto b = flag();
        if (!b) return std::unexpected(b.error());
        f.region = *b ? Region::Pal : Region::Ntsc;
    } else if (key == "binary") {
        const auto b = flag();
        if (!b) return std::unexpected(b.error());
        if (*b) return std::unexpected(refuse(Refusal::Binary, ERR_SITE()));
    } else if (key == "FDS") {
        const auto b = flag();
        if (!b) return std::unexpected(b.error());
        if (*b) return std::unexpected(refuse(Refusal::Disk, ERR_SITE()));
    } else if (key == "fourscore") {
        const auto b = flag();
        if (!b) return std::unexpected(b.error());
        if (*b) return std::unexpected(refuse(Refusal::Multitap, ERR_SITE()));
    } else if (key == "port0" || key == "port1") {

        const auto v = decimal(val, 2);
        if (!v || *v > 1) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
        const std::uint8_t bit = key == "port0" ? 0x1u : 0x2u;
        f.ports = static_cast<std::uint8_t>(*v == 1 ? (f.ports | bit) : (f.ports & ~bit));
    } else if (key == "port2") {
        const auto v = decimal(val, 2);
        if (!v || *v != 0) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    } else if (key == "romChecksum") {
        const auto d = decode_digest(val);
        if (!d) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
        f.digest = DigestValue{.kind = DigestKind::Md5, .len = 16};
        std::copy(d->begin(), d->end(), f.digest.bytes.begin());
        f.has_digest = true;
    }
    return f;
}

Ex<void> NesMovieCodec::finish_header(const Facts& f) const noexcept {
    if (f.version != 3) return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
    if (f.ports == 0) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    if (!f.has_digest) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> NesMovieCodec::frame(std::string_view line, const Facts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kLineMax || !starts_log(line)) {
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    }

    std::array<std::string_view, 4> fld{};
    std::string_view rest = line.substr(1);
    for (std::size_t i = 0; i < fld.size(); ++i) {
        const std::size_t bar = rest.find('|');
        if (bar == std::string_view::npos)
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        fld[i] = rest.substr(0, bar);
        rest.remove_prefix(bar + 1);
    }
    if (!rest.empty()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    const auto cmd = decimal(fld[0], 3);
    if (!cmd || *cmd > 0xFFu) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    Frame out{};
    out.commands = static_cast<std::uint8_t>(*cmd);
    for (std::size_t p = 0; p < 2; ++p) {
        const std::string_view pad = fld[1 + p];
        if ((f.ports & (1u << p)) == 0) {
            if (!pad.empty()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            continue;
        }
        if (pad.size() != kPadBit.size())
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < pad.size(); ++i) {
            const char c = pad[i];
            if (c < 0x20 || c > 0x7E) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            if (c != ' ' && c != '.') m |= 1u << kPadBit[i];
        }
        out.mask[p] = m;
    }
    if (!fld[3].empty()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    return out;
}

IMovieCodec::SettingNeeds NesMovieCodec::setting_needs(const Facts& f) const noexcept {
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add({.lo = r.lo,
                 .width = r.width,
                 .allowed = r.allowed,
                 .preferred = static_cast<std::uint8_t>(std::countr_zero(r.allowed)),
                 .name = r.name});
    }

    const std::uint8_t want = f.region == Region::Pal ? 2u : 1u;
    out.add({.lo = kSysTypeLo,
             .width = 2,
             .allowed = static_cast<std::uint16_t>(1u << want),
             .preferred = want,
             .name = "System Type"});
    return out;
}

IMovieCodec::Raster NesMovieCodec::raster(const Facts& f) const noexcept {

    if (f.region == Region::Pal) return {.period_ns = 19'997'209, .line0_ns = 2'755'893};
    return {.period_ns = 16'639'265, .line0_ns = 1'143'173};
}

IMovieCodec::PowerOn NesMovieCodec::power_on(const Facts& f) const noexcept {

    return {.clocks = kRamClearClocks,
            .clock_hz = f.region == Region::Pal ? kClkPalHz : kClkNtscHz,
            .lo = kRamClearLo,
            .width = 2,
            .when = 0x0E};
}

std::optional<IMovieCodec::DigestSpan> NesMovieCodec::rom_digest_span(
    std::span<const std::uint8_t> head, std::uint64_t size) const noexcept {
    if (head.size() < kRomHead || head[0] != 'N' || head[1] != 'E' || head[2] != 'S' ||
        head[3] != 0x1A) {
        return std::nullopt;
    }

    const bool nes2 = (head[7] & 0x0Cu) == 0x08u;
    const unsigned prg_hi = nes2 ? (head[9] & 0x0Fu) : 0u;
    const unsigned chr_hi = nes2 ? (head[9] >> 4) : 0u;
    if (prg_hi == 0x0Fu || chr_hi == 0x0Fu) return std::nullopt;
    const std::uint64_t prg = (std::uint64_t{prg_hi} << 8 | head[4]) * 16384u;
    const std::uint64_t chr = (std::uint64_t{chr_hi} << 8 | head[5]) * 8192u;
    const std::uint64_t offset = kRomHead + ((head[6] & 0x04u) != 0 ? 512u : 0u);
    if (prg + chr == 0 || offset + prg + chr > size) return std::nullopt;
    return DigestSpan{.offset = offset, .length = prg + chr};
}

}  // namespace mister::cores
