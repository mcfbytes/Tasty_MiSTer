// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_bk2_codec.h"

#include <charconv>
#include <system_error>

namespace mister::cores {
namespace {

bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

bool truthy(std::string_view v) { return v == "1" || v == "True" || v == "true"; }
bool falsy(std::string_view v) { return v == "0" || v == "False" || v == "false"; }

constexpr std::string_view kLogKeyPrefix = "LogKey:";

}  // namespace

bool SnesBk2Codec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

bool SnesBk2Codec::ends_log(std::string_view line) const noexcept {
    return trim(line) == "[/Input]";
}

Ex<std::uint8_t> SnesBk2Codec::column_of(std::string_view name) noexcept {
    if (name == "Reset") return kColReset;
    if (name == "Power") return kColPower;
    if (name == "Subframe") return kColSubframe;
    if (name == "Reset Instruction") return kColResetDelay;

    if (name.size() < 4 || name[0] != 'P' || name[1] < '1' || name[1] > '9' || name[2] != ' ')
        return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    const unsigned port = static_cast<unsigned>(name[1] - '1');
    for (const PadName& p : kPadNames) {
        if (name.substr(3) != p.name) continue;
        if (port > 1) return std::unexpected(refuse(Refusal::Multitap, ERR_SITE()));
        return static_cast<std::uint8_t>(kColPad | port << 4 | static_cast<unsigned>(p.button));
    }
    return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
}

Ex<IMovieCodec::Facts> SnesBk2Codec::header_line(std::string_view line,
                                                 const Facts& so_far) const noexcept {
    Facts f = so_far;
    line = trim(line);
    if (starts_with(line, kLogKeyPrefix)) {
        if ((f.layout & kLogKey) != 0)
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        std::string_view key = line.substr(kLogKeyPrefix.size());
        const std::size_t first = key.find('#');
        if (first == std::string_view::npos)
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        key.remove_prefix(first + 1);
        std::size_t n = 0;
        std::uint8_t ports = 0;
        while (true) {
            const std::size_t hash = key.find('#');
            std::string_view group = key.substr(0, hash);
            while (!group.empty()) {
                const std::size_t bar = group.find('|');
                const std::string_view name = group.substr(0, bar);
                group.remove_prefix(bar == std::string_view::npos ? group.size() : bar + 1);
                if (name.empty()) continue;
                const auto c = column_of(name);
                if (!c) return std::unexpected(c.error());
                if (n + 1 >= kColumnsMax)
                    return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
                f.columns[n++] = *c;
                if ((*c & kColPad) != 0)
                    ports |= static_cast<std::uint8_t>(1u << ((*c >> 4) & 0x7u));
            }
            if (n >= kColumnsMax) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
            f.columns[n++] = kColGroupEnd;
            if (hash == std::string_view::npos) break;
            key.remove_prefix(hash + 1);
        }
        f.columns_n = static_cast<std::uint8_t>(n);
        f.ports = ports;
        f.layout |= kLogKey;
        return f;
    }
    const auto [key, val] = split(line);

    if (key == "@Core" || key == "@CoreText" || key == "@SaveRam" ||
        key == "SavestateBinaryBase64Blob" ||
        ((key == "StartsFromSavestate" || key == "StartsFromSaveRam") && truthy(val))) {
        return std::unexpected(refuse(Refusal::Savestate, ERR_SITE()));
    }
    if (key == "MovieVersion") {
        if (!starts_with(val, "BizHawk v2"))
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        f.layout |= kMovieVersion;
    } else if (key == "Platform") {
        if (val != "SNES") return std::unexpected(refuse(Refusal::System, ERR_SITE()));
        f.layout |= kPlatform;
    } else if (key == "PAL") {
        if (!truthy(val) && !falsy(val))
            return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
        f.region = truthy(val) ? Region::Pal : Region::Ntsc;
    } else if (key == "SyncSettings.o.LeftPort" || key == "SyncSettings.LeftPort") {

        if (val == "0" || val == "None" || val == "Unplugged")
            return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    } else if (key == "Core") {
        if (val == "BSNESv115+" || val == "SubBSNESv115+") f.layout |= kBsnes115;
        if (val == "BSNES") f.layout |= kLibsnes;
    } else if (key == "SyncSettings.o.RandomizedInitialState" ||
               key == "SyncSettings.RandomizedInitialState") {
        if (!truthy(val) && !falsy(val))
            return std::unexpected(refuse(Refusal::Setting, ERR_SITE()));
        if (falsy(val)) f.layout |= kLibsnesFlat;
    } else if (key == "SyncSettings.o.Entropy" || key == "SyncSettings.Entropy") {
        const int e = val == "0" || val == "None"   ? 0
                      : val == "1" || val == "Low"  ? 1
                      : val == "2" || val == "High" ? 2
                                                    : -1;
        if (e < 0) return std::unexpected(refuse(Refusal::Setting, ERR_SITE()));
        f.layout =
            static_cast<std::uint16_t>((f.layout & ~kEntropyMask) | (e + 1) << kEntropyShift);
    } else if (key == "SHA1" || ((key == "SHA256" || key == "MD5") && (f.layout & kSha1) == 0)) {

        const DigestKind kind = key == "SHA256"                    ? DigestKind::Sha256
                                : key == "MD5" || val.size() == 32 ? DigestKind::Md5
                                : val.size() == 8                  ? DigestKind::Crc32
                                                                   : DigestKind::Sha1;
        const auto d = parse_hex_digest(val, kind);
        if (!d) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
        f.digest = *d;
        f.has_digest = true;
        if (key == "SHA1") f.layout |= kSha1;
    }
    note_rerecords(key, val, f);
    return f;
}

Ex<void> SnesBk2Codec::finish_header(const Facts& f) const noexcept {
    constexpr std::uint8_t kNeeded = kMovieVersion | kPlatform | kLogKey;
    if ((f.layout & kNeeded) != kNeeded)
        return std::unexpected(refuse(Refusal::NotAMovie, ERR_SITE()));
    if (f.ports == 0) return std::unexpected(refuse(Refusal::PortType, ERR_SITE()));
    if (!f.has_digest) return std::unexpected(refuse(Refusal::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> SnesBk2Codec::frame(std::string_view line, const Facts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kLineMax || !starts_log(line))
        return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    Frame out{};
    std::size_t at = 1;
    for (std::size_t i = 0; i < f.columns_n && i < f.columns.size(); ++i) {
        const std::uint8_t c = f.columns[i];
        if (at >= line.size()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        if (c == kColGroupEnd) {
            if (line[at++] != '|') return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            continue;
        }
        if (c == kColResetDelay) {

            const std::size_t comma = line.find(',', at);
            if (comma == std::string_view::npos || comma - at > 12)
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            std::string_view v = line.substr(at, comma - at);
            while (!v.empty() && v.front() == ' ')
                v.remove_prefix(1);
            int n = 0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
            if (v.empty() || ec != std::errc{} || end != v.data() + v.size())
                return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
            at = comma + 1;
            continue;
        }
        const char ch = line[at++];
        if (ch <= 0x20 || ch > 0x7E || ch == '|')
            return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
        if (ch == '.') continue;
        if (c == kColReset) {
            out.commands |= 0x1u;
        } else if (c == kColPower) {
            out.commands |= 0x2u;
        } else if (c == kColSubframe) {
            return std::unexpected(refuse(Refusal::Subframe, ERR_SITE()));
        } else if ((c & kColPad) != 0) {
            out.mask[(c >> 4) & 0x7u] |= 1u << (c & 0xFu);
        }
    }
    if (at != line.size()) return std::unexpected(refuse(Refusal::BadLine, ERR_SITE()));
    return out;
}

std::optional<IMovieCodec::DigestSpan> SnesBk2Codec::rom_digest_span(
    std::span<const std::uint8_t>, std::uint64_t size) const noexcept {
    const std::uint64_t rem = size % 1024u;
    return past_header(size, rem == 128u || rem == 512u ? rem : 0u);
}

RamImageRecipe SnesBk2Codec::do_recorder_ram(const Facts& f) const noexcept {

    if ((f.layout & kLibsnes) != 0) {
        if ((f.layout & kLibsnesFlat) != 0)
            return {.kind = RamImageRecipe::Kind::Flat,
                    .first_fill = 0x55,
                    .second_fill = 0x00,
                    .first_bytes = kWramBytes,
                    .second_bytes = kAramBytes};
        return {.kind = RamImageRecipe::Kind::Lfsr,
                .seed = 0,
                .first_bytes = kWramBytes,
                .second_bytes = kAramBytes};
    }
    if ((f.layout & kBsnes115) == 0) return {};
    const unsigned set = (f.layout & kEntropyMask) >> kEntropyShift;

    const auto entropy =
        set == 0 ? RamImageRecipe::Entropy::Low : static_cast<RamImageRecipe::Entropy>(set - 1);
    return {.kind = RamImageRecipe::Kind::Pcg32,
            .entropy = entropy,
            .second_fill = 0x00,
            .seed = kSandboxClockSeed,
            .first_bytes = kWramBytes,
            .second_bytes = kAramBytes};
}

}  // namespace mister::cores
