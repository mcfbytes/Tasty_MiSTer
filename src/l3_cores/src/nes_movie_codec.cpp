// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nes_movie_codec.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <system_error>

namespace mister::cores {
namespace {

Error refuse(IMovieCodec::Refusal r, infra::ErrorSite site) {
    return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
}

std::optional<std::uint32_t> decimal(std::string_view s, std::size_t digits) {
    if (s.empty() || s.size() > digits)
        return std::nullopt;
    std::uint32_t v = 0;
    const char* const end = s.data() + s.size();
    const auto [at, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || at != end)
        return std::nullopt;
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
    constexpr int kB64Lower = 26;
    constexpr int kB64Digit = 52;
    constexpr int kB64Plus = 62;
    constexpr int kB64Slash = 63;
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + kB64Lower;
    if (c >= '0' && c <= '9')
        return c - '0' + kB64Digit;
    if (c == '+')
        return kB64Plus;
    if (c == '/')
        return kB64Slash;
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

}

bool NesMovieCodec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

std::optional<std::array<std::uint8_t, 16>> NesMovieCodec::decode_digest(
    std::string_view text
) noexcept {
    constexpr unsigned kB64Bits = 6;
    constexpr unsigned kByteBits = 8;
    constexpr std::uint32_t kByteMask = 0xFF;
    constexpr std::size_t kDigestBytes = 16;
    constexpr std::size_t kB64Chars = 22;
    constexpr std::size_t kB64Padded = 24;
    constexpr std::string_view kPrefix = "base64:";
    if (text.substr(0, kPrefix.size()) != kPrefix)
        return std::nullopt;
    text.remove_prefix(kPrefix.size());
    if (text.size() != kB64Padded || text.substr(kB64Chars) != "==")
        return std::nullopt;
    std::array<std::uint8_t, kDigestBytes> out{};
    std::uint32_t acc = 0;
    unsigned bits = 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < kB64Chars; ++i) {
        const int v = b64(text[i]);
        if (v < 0)
            return std::nullopt;
        acc = (acc << kB64Bits) | static_cast<std::uint32_t>(v);
        bits += kB64Bits;
        if (bits >= kByteBits) {
            bits -= kByteBits;
            if (n < out.size())
                out[n++] = static_cast<std::uint8_t>((acc >> bits) & kByteMask);
        }
    }
    if (n != out.size())
        return std::nullopt;
    return out;
}

Ex<IMovieCodec::Facts> NesMovieCodec::header_line(
    std::string_view line,
    const Facts& so_far
) const noexcept {
    constexpr std::size_t kFlagDigits = 1;
    constexpr std::size_t kVersionDigits = 3;
    constexpr std::uint16_t kMovieVersion = 3;
    constexpr std::size_t kPortDigits = 2;
    constexpr std::uint8_t kPort0Bit = 0x1;
    constexpr std::uint8_t kPort1Bit = 0x2;
    constexpr std::size_t kDigestBytes = 16;
    Facts f = so_far;
    line = trim(line);
    if (line.empty())
        return f;
    const std::size_t sp = line.find(' ');
    const std::string_view key = line.substr(0, sp);
    const std::string_view val = sp == std::string_view::npos ? std::string_view{}
                                                              : trim(line.substr(sp + 1));

    if (key == "savestate")
        return std::unexpected(refuse(Refusal::Savestate, ERR_SITE()));
    if (line.size() > kLineMax)
        return f;
    const auto flag = [&]() -> Ex<bool> {
        const auto v = decimal(val, kFlagDigits);
        if (!v || *v > 1)
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        return *v == 1;
    };
    if (key == "version") {
        const auto v = decimal(val, kVersionDigits);
        if (!v || *v != kMovieVersion)
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        f.version = static_cast<std::uint16_t>(*v);
    } else if (key == "palFlag") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        f.region = *b ? Region::Pal : Region::Ntsc;
    } else if (key == "binary") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(refuse(Refusal::Binary, ERR_SITE()));
    } else if (key == "FDS") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(refuse(Refusal::Disk, ERR_SITE()));
    } else if (key == "fourscore") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(refuse(Refusal::Multitap, ERR_SITE()));
    } else if (key == "port0" || key == "port1") {

        const auto v = decimal(val, kPortDigits);
        if (!v || *v > 1)
            return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
        const std::uint8_t bit = key == "port0" ? kPort0Bit : kPort1Bit;
        f.ports = static_cast<std::uint8_t>(*v == 1 ? (f.ports | bit) : (f.ports & ~bit));
    } else if (key == "port2") {
        const auto v = decimal(val, kPortDigits);
        if (!v || *v != 0)
            return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    } else if (key == "romChecksum") {
        const auto d = decode_digest(val);
        if (!d)
            return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
        f.digest = DigestValue{.kind = DigestKind::Md5, .len = kDigestBytes};
        std::copy(d->begin(), d->end(), f.digest.bytes.begin());
        f.has_digest = true;
    }
    note_rerecords(key, val, f);
    return f;
}

Ex<void> NesMovieCodec::finish_header(const Facts& f) const noexcept {
    constexpr std::uint16_t kMovieVersion = 3;
    if (f.version != kMovieVersion)
        return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
    if (f.ports == 0)
        return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    if (!f.has_digest)
        return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> NesMovieCodec::frame(std::string_view line, const Facts& f) const noexcept {
    constexpr std::size_t kCommandDigits = 3;
    constexpr std::size_t kPads = 2;
    constexpr std::uint8_t kPrintableLo = 0x20;
    constexpr std::uint8_t kPrintableHi = 0x7E;
    constexpr std::uint32_t kByteMask = 0xFF;
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
    if (!rest.empty())
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    const auto cmd = decimal(fld[0], kCommandDigits);
    if (!cmd || *cmd > kByteMask)
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    Frame out{};
    out.commands = static_cast<std::uint8_t>(*cmd);
    for (std::size_t p = 0; p < kPads; ++p) {
        const std::string_view pad = fld[1 + p];
        if ((f.ports & (1u << p)) == 0) {
            if (!pad.empty())
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            continue;
        }
        if (pad.size() != kPadBit.size())
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < pad.size(); ++i) {
            const char c = pad[i];
            if (c < kPrintableLo || c > kPrintableHi)
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            if (c != ' ' && c != '.')
                m |= 1u << kPadBit[i];
        }
        out.mask[p] = m;
    }
    if (!fld[3].empty())
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    return out;
}

IMovieCodec::SettingNeeds NesMovieCodec::setting_needs(const Facts& f) const noexcept {
    constexpr std::uint8_t kOptionWidth = 2;
    constexpr std::uint8_t kSysNtsc = 1;
    constexpr std::uint8_t kSysPal = 2;
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add(
            {.lo = r.lo,
             .width = r.width,
             .allowed = r.allowed,
             .preferred = static_cast<std::uint8_t>(std::countr_zero(r.allowed)),
             .name = r.name}
        );
    }

    const std::uint8_t want = f.region == Region::Pal ? kSysPal : kSysNtsc;
    out.add(
        {.lo = kSysTypeLo,
         .width = kOptionWidth,
         .allowed = static_cast<std::uint16_t>(1u << want),
         .preferred = want,
         .name = "System Type"}
    );
    return out;
}

std::optional<IMovieCodec::SettingNeed> NesMovieCodec::ram_fill_need(RamFill fill) const noexcept {
    constexpr std::uint8_t kOptionWidth = 2;
    constexpr std::uint8_t kRamClearZero = 1;
    constexpr std::uint8_t kRamClearFf = 2;
    constexpr std::uint8_t kRamClearRandom = 3;

    const auto v = static_cast<std::uint8_t>(
        fill == RamFill::Zero     ? kRamClearZero
            : fill == RamFill::Ff ? kRamClearFf
                                  : kRamClearRandom
    );
    return SettingNeed{
        .lo = kRamClearLo,
        .width = kOptionWidth,
        .allowed = static_cast<std::uint16_t>(1u << v),
        .preferred = v,
        .name = "RAM Clear"
    };
}

IMovieCodec::Raster NesMovieCodec::raster(const Facts& f) const noexcept {
    constexpr std::uint32_t kPalFramePeriodNs = 19'997'209;
    constexpr std::uint32_t kPalLine0Ns = 2'755'893;
    constexpr std::uint32_t kNtscFramePeriodNs = 16'639'265;
    constexpr std::uint32_t kNtscLine0Ns = 1'143'173;

    if (f.region == Region::Pal)
        return {.period_ns = kPalFramePeriodNs, .line0_ns = kPalLine0Ns};
    return {.period_ns = kNtscFramePeriodNs, .line0_ns = kNtscLine0Ns};
}

IMovieCodec::PowerOn NesMovieCodec::power_on(const Facts& f) const noexcept {
    constexpr std::uint8_t kOptionWidth = 2;
    constexpr std::uint8_t kRamClearWhen = 0x0E;

    return {
        .clocks = kRamClearClocks,
        .clock_hz = f.region == Region::Pal ? kClkPalHz : kClkNtscHz,
        .lo = kRamClearLo,
        .width = kOptionWidth,
        .when = kRamClearWhen
    };
}

std::optional<IMovieCodec::DigestSpan> NesMovieCodec::rom_digest_span(
    std::span<const std::uint8_t> head,
    proto::FileSize size
) const noexcept {
    constexpr std::size_t kInesEofAt = 3;
    constexpr std::uint8_t kInesEof = 0x1A;
    constexpr std::size_t kInesPrgAt = 4;
    constexpr std::size_t kInesChrAt = 5;
    constexpr std::size_t kInesFlags6At = 6;
    constexpr std::size_t kInesFlags7At = 7;
    constexpr std::size_t kInesSizeMsbAt = 9;
    constexpr unsigned kInesFormatMask = 0x0C;
    constexpr unsigned kInesNes2 = 0x08;
    constexpr unsigned kInesTrainerBit = 0x04;
    constexpr unsigned kNibbleMask = 0x0F;
    constexpr unsigned kNibbleBits = 4;
    constexpr std::uint64_t kPrgBankBytes = 16384;
    constexpr std::uint64_t kChrBankBytes = 8192;
    constexpr std::uint64_t kTrainerBytes = 512;
    constexpr unsigned kByteBits = 8;
    const std::uint64_t bytes = size.v;
    if (head.size() < kRomHead
        || head[0] != 'N'
        || head[1] != 'E'
        || head[2] != 'S'
        || head[kInesEofAt] != kInesEof) {
        return std::nullopt;
    }

    const bool nes2 = (head[kInesFlags7At] & kInesFormatMask) == kInesNes2;
    const unsigned prg_hi = nes2 ? (head[kInesSizeMsbAt] & kNibbleMask) : 0u;
    const unsigned chr_hi = nes2 ? (head[kInesSizeMsbAt] >> kNibbleBits) : 0u;
    if (prg_hi == kNibbleMask || chr_hi == kNibbleMask)
        return std::nullopt;
    const std::uint64_t prg = (std::uint64_t{prg_hi} << kByteBits | head[kInesPrgAt])
        * kPrgBankBytes;
    const std::uint64_t chr = (std::uint64_t{chr_hi} << kByteBits | head[kInesChrAt])
        * kChrBankBytes;
    const std::uint64_t offset = kRomHead
        + ((head[kInesFlags6At] & kInesTrainerBit) != 0 ? kTrainerBytes : 0u);
    if (prg + chr == 0 || offset + prg + chr > bytes)
        return std::nullopt;
    return DigestSpan{.offset = offset, .length = prg + chr};
}

}
