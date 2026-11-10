// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/nes_fm2_codec.h"
#include "cores/nes_movie_codec.h"

#include "infra/parse_int.h"

#include <bit>

namespace mister::cores {
namespace {

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
    proto::BitField field;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr proto::BitField kRamClearField{proto::StatusBit{65}, 2};
constexpr std::uint8_t kRamClearZero = 1;
constexpr std::uint8_t kRamClearFf = 2;
constexpr std::uint8_t kRamClearRandom = 3;

constexpr std::uint8_t ram_clear_value(IReplayCore::RamFill fill) noexcept {
    switch (fill) {
        case IReplayCore::RamFill::Zero:
            return kRamClearZero;
        case IReplayCore::RamFill::Ff:
            return kRamClearFf;
        case IReplayCore::RamFill::Random:
            return kRamClearRandom;
    }
    __builtin_unreachable();
}

constexpr std::array<SettingRule, 7> kRules{{
    {{proto::StatusBit{9}, 1}, 0x01, "Swap Joysticks"},
    {{proto::StatusBit{51}, 2}, 0x01, "SNAC"},
    {{proto::StatusBit{10}, 1}, 0x01, "Multitap"},
    {{proto::StatusBit{32}, 3}, 0x01, "Peripheral"},
    {{proto::StatusBit{53}, 1}, 0x01, "Famicom Keyboard"},
    {{proto::StatusBit{41}, 1}, 0x01, "Pause when OSD is open"},
    {kRamClearField, 0x06, "RAM Clear"},
}};

constexpr proto::BitField kSysTypeField{proto::StatusBit{23}, 2};

constexpr std::uint64_t kRamClearClocks = 0xF'FFFFull * 16u + 259u;

constexpr std::uint32_t kClkNtscHz = 21'477'272;
constexpr std::uint32_t kClkPalHz = 21'281'370;

}

bool NesFm2Codec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

std::optional<DigestValue> NesFm2Codec::decode_digest(std::string_view text) noexcept {
    constexpr unsigned kB64Bits = 6;
    constexpr unsigned kByteBits = 8;
    constexpr std::uint32_t kByteMask = 0xFF;
    constexpr std::size_t kDigestBytes = 16;
    constexpr std::size_t kB64Chars = 22;
    constexpr std::size_t kB64Padded = 24;
    constexpr std::string_view kPrefix = "base64:";
    if (!text.starts_with(kPrefix))
        return std::nullopt;
    text.remove_prefix(kPrefix.size());
    if (text.size() != kB64Padded || text.substr(kB64Chars) != "==")
        return std::nullopt;
    DigestValue out{.kind = DigestKind::Md5};
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
            if (n < kDigestBytes) {
                out.bytes[n++] = static_cast<std::uint8_t>((acc >> bits) & kByteMask);
            }
        }
    }
    if (n != kDigestBytes)
        return std::nullopt;
    return out;
}

Ex<MovieFacts> NesFm2Codec::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    constexpr std::size_t kFlagDigits = 1;
    constexpr std::size_t kVersionDigits = 3;
    constexpr std::uint16_t kMovieVersion = 3;
    constexpr std::size_t kPortDigits = 2;
    MovieFacts f = so_far;
    line = IMovieFormat::trim(line);
    if (line.empty())
        return f;
    const std::size_t sp = line.find(' ');
    const std::string_view key = line.substr(0, sp);
    const std::string_view val = sp == std::string_view::npos
        ? std::string_view{}
        : IMovieFormat::trim(line.substr(sp + 1));

    if (key == "savestate")
        return std::unexpected(movie_refusal(MovieRefusal::Savestate, ERR_SITE()));
    if (line.size() > kMovieLineMax)
        return f;
    const auto flag = [&]() -> Ex<bool> {
        const auto v = val.size() <= kFlagDigits ? infra::parse_int<std::uint32_t>(val)
                                                 : std::nullopt;
        if (!v || *v > 1)
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        return *v == 1;
    };
    if (key == "version") {
        const auto v = val.size() <= kVersionDigits ? infra::parse_int<std::uint32_t>(val)
                                                    : std::nullopt;
        if (!v || *v != kMovieVersion)
            return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
        f.version = MovieRevision{static_cast<std::uint16_t>(*v)};
    } else if (key == "palFlag") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        f.region = *b ? MovieRegion::Pal : MovieRegion::Ntsc;
    } else if (key == "binary") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(movie_refusal(MovieRefusal::Binary, ERR_SITE()));
    } else if (key == "FDS") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(movie_refusal(MovieRefusal::Disk, ERR_SITE()));
    } else if (key == "fourscore") {
        const auto b = flag();
        if (!b)
            return std::unexpected(b.error());
        if (*b)
            return std::unexpected(movie_refusal(MovieRefusal::Multitap, ERR_SITE()));
    } else if (key == "port0" || key == "port1") {

        const auto v = val.size() <= kPortDigits ? infra::parse_int<std::uint32_t>(val)
                                                 : std::nullopt;
        if (!v || *v > 1)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
        const svc::PlayerIndex slot = svc::kPlayers[key == "port0" ? 0 : 1];
        f.ports = *v == 1 ? f.ports.with(slot) : ports_without(f.ports, slot);
    } else if (key == "port2") {
        const auto v = val.size() <= kPortDigits ? infra::parse_int<std::uint32_t>(val)
                                                 : std::nullopt;
        if (!v || *v != 0)
            return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    } else if (key == "romChecksum") {
        const auto d = decode_digest(val);
        if (!d)
            return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
        f.digest = *d;
    }
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    return f;
}

Ex<void> NesFm2Codec::finish_header(const MovieFacts& f) const noexcept {
    constexpr std::uint16_t kMovieVersion = 3;
    if (f.version.number() != kMovieVersion)
        return std::unexpected(movie_refusal(MovieRefusal::NotAMovie, ERR_SITE()));
    if (f.ports.empty())
        return std::unexpected(movie_refusal(MovieRefusal::PortType, ERR_SITE()));
    if (!f.digest)
        return std::unexpected(movie_refusal(MovieRefusal::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> NesFm2Codec::frame(std::string_view line, const MovieFacts& f) const noexcept {
    constexpr std::size_t kCommandDigits = 3;
    constexpr std::size_t kPads = 2;
    constexpr std::uint8_t kPrintableLo = 0x20;
    constexpr std::uint8_t kPrintableHi = 0x7E;
    constexpr std::uint32_t kByteMask = 0xFF;
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kMovieLineMax || !starts_log(line)) {
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    }

    std::array<std::string_view, 4> fld{};
    std::string_view rest = line.substr(1);
    for (std::size_t i = 0; i < fld.size(); ++i) {
        const std::size_t bar = rest.find('|');
        if (bar == std::string_view::npos)
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        fld[i] = rest.substr(0, bar);
        rest.remove_prefix(bar + 1);
    }
    if (!rest.empty())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    const auto cmd = fld[0].size() <= kCommandDigits ? infra::parse_int<std::uint32_t>(fld[0])
                                                     : std::nullopt;
    if (!cmd || *cmd > kByteMask)
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));

    MovieFrame out{};
    out.commands = MovieCommandMask::of_bits(static_cast<std::uint8_t>(*cmd));
    for (std::size_t p = 0; p < kPads; ++p) {
        const std::string_view pad = fld[1 + p];
        if (!f.ports.has(svc::kPlayers[p])) {
            if (!pad.empty())
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            continue;
        }
        if (pad.size() != kPadBit.size())
            return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
        proto::JoyMask m{};
        for (std::size_t i = 0; i < pad.size(); ++i) {
            const char c = pad[i];
            if (c < kPrintableLo || c > kPrintableHi)
                return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
            if (c != ' ' && c != '.')
                m = m.with(kPadBit[i].mask());
        }
        out.mask[p] = m;
    }
    if (!fld[3].empty())
        return std::unexpected(movie_refusal(MovieRefusal::BadLine, ERR_SITE()));
    return out;
}

IReplayCore::SettingNeeds NesMovieCodec::setting_needs(const MovieFacts& f) const noexcept {
    constexpr std::uint8_t kSysNtsc = 1;
    constexpr std::uint8_t kSysPal = 2;
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add(
            {.field = r.field,
             .allowed = proto::ValueSet{r.allowed},
             .preferred = proto::FieldValue{static_cast<std::uint32_t>(
                 std::countr_zero(r.allowed)
             )},
             .name = r.name}
        );
    }

    const std::uint8_t want = f.region == MovieRegion::Pal ? kSysPal : kSysNtsc;
    out.add(
        {.field = kSysTypeField,
         .allowed = proto::ValueSet{static_cast<std::uint16_t>(1u << want)},
         .preferred = proto::FieldValue{want},
         .name = "System Type"}
    );
    return out;
}

std::optional<IReplayCore::SettingNeed> NesMovieCodec::ram_fill_need(RamFill fill) const noexcept {

    const std::uint8_t v = ram_clear_value(fill);
    return SettingNeed{
        .field = kRamClearField,
        .allowed = proto::ValueSet{static_cast<std::uint16_t>(1u << v)},
        .preferred = proto::FieldValue{v},
        .name = "RAM Clear"
    };
}

IReplayCore::Raster NesMovieCodec::raster(const MovieFacts& f) const noexcept {
    constexpr std::uint32_t kPalFramePeriodNs = 19'997'209;
    constexpr std::uint32_t kPalLine0Ns = 2'755'893;
    constexpr std::uint32_t kNtscFramePeriodNs = 16'639'265;
    constexpr std::uint32_t kNtscLine0Ns = 1'143'173;

    if (f.region == MovieRegion::Pal)
        return {
            .period_ns = std::chrono::nanoseconds{kPalFramePeriodNs},
            .line0_ns = std::chrono::nanoseconds{kPalLine0Ns}
        };
    return {
        .period_ns = std::chrono::nanoseconds{kNtscFramePeriodNs},
        .line0_ns = std::chrono::nanoseconds{kNtscLine0Ns}
    };
}

IReplayCore::PowerOn NesMovieCodec::power_on(const MovieFacts& f) const noexcept {
    constexpr std::uint8_t kRamClearWhen = 0x0E;

    return {
        .clocks = CoreClocks{kRamClearClocks},
        .clock_hz = ClockHz{f.region == MovieRegion::Pal ? kClkPalHz : kClkNtscHz},
        .field = kRamClearField,
        .when = proto::ValueSet{kRamClearWhen}
    };
}

std::optional<IMovieFormat::DigestSpan> NesFm2Codec::rom_digest_span(
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
    if (prg + chr == 0 || proto::FileSize{offset + prg + chr} > size)
        return std::nullopt;
    return DigestSpan{.offset = proto::FileSize{offset}, .length = proto::FileSize{prg + chr}};
}

}
