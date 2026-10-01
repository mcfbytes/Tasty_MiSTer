// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_movie_codec.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <system_error>
#include <vector>

#include "cores/manifests/psx.h"
#include "cores/movie_archive.h"
#include "svc/chd_source.h"
#include "svc/disc_engine.h"
#include "svc/file.h"
#include "svc/vfs.h"
#include "svc/xml_scan.h"

namespace mister::cores {
namespace {

using CR = IMovieCodec::Refusal;
using Emu = PsxMovieCodec::Emu;
using Layout = PsxMovieCodec::Layout;

Error refuse(CR r, std::uint16_t site) {
    return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view strip(std::string_view s) {
    while (!s.empty() && is_space(s.back()))
        s.remove_suffix(1);
    while (!s.empty() && is_space(s.front()))
        s.remove_prefix(1);
    return s;
}

struct Fields {
    std::array<std::string_view, 4> f{};
    std::string_view rest{};
};
Fields fields(std::string_view s) {
    Fields out{};
    s = strip(s);
    for (std::size_t i = 0; i < out.f.size() && !s.empty(); ++i) {
        const std::size_t sp = std::min(s.find(' '), s.find('\t'));
        out.f[i] = s.substr(0, sp);
        s = strip(s.substr(out.f[i].size()));
        if (i == 0) out.rest = s;
    }
    return out;
}

std::optional<std::uint32_t> decimal(std::string_view s, std::size_t digits) {
    if (s.empty() || s.size() > digits) return std::nullopt;
    std::uint32_t v = 0;
    const char* const end = s.data() + s.size();
    const auto [at, ec] = std::from_chars(s.data(), end, v);
    if (ec != std::errc{} || at != end) return std::nullopt;
    return v;
}

std::optional<bool> boolean(std::string_view v) {
    if (v == "1" || svc::xml::iequal(v, "true")) return true;
    if (v == "0" || svc::xml::iequal(v, "false")) return false;
    return std::nullopt;
}

std::optional<std::uint8_t> column_role(std::string_view name) {
    if (name == "Disc Select" || name == "Disk Index") return PsxMovieCodec::kColValue;
    if (name == "Open" || name == "Close" || name == "Open Tray" || name == "Close Tray")
        return PsxMovieCodec::kColTray;
    if (name == "Reset") return PsxMovieCodec::kColReset;
    if (name == "Power") return PsxMovieCodec::kColPower;
    if (!name.starts_with("P1 ")) return std::nullopt;
    name.remove_prefix(3);
    for (const PsxMovieCodec::Button& b : PsxMovieCodec::kButtons) {
        if (name == b.octoshock || name == b.nymashock) return b.bit;
    }
    return std::nullopt;
}

std::optional<IMovieCodec::Facts> read_log_key(std::string_view key, IMovieCodec::Facts f) {
    f.columns_n = 0;
    unsigned bits = 0;
    while (!key.empty()) {
        const std::size_t end = key.find_first_of("|#");
        const std::string_view name = key.substr(0, end);
        key.remove_prefix(end == std::string_view::npos ? key.size() : end + 1);
        if (name.empty()) continue;
        const auto role = column_role(name);
        if (!role || f.columns_n == f.columns.size()) return std::nullopt;
        if (*role < 14) {
            if ((bits >> *role) & 1u) return std::nullopt;
            bits |= 1u << *role;
        }
        f.columns[f.columns_n++] = *role;
    }
    if (bits != 0x3FFFu) return std::nullopt;
    return f;
}

struct CueTrack {
    std::uint8_t number = 0;
    bool data = false;
    bool mode2 = false;
    std::uint8_t flags = 0;
    std::uint16_t file = 0;
    std::optional<std::uint32_t> index1{};
};
struct Cue {
    std::vector<std::string> files;
    std::vector<CueTrack> tracks;
};

std::optional<std::uint32_t> msf(std::string_view s) {
    if (s.size() != 8 || s[2] != ':' || s[5] != ':') return std::nullopt;
    const auto m = decimal(s.substr(0, 2), 2);
    const auto sec = decimal(s.substr(3, 2), 2);
    const auto fr = decimal(s.substr(6, 2), 2);
    if (!m || !sec || !fr || *sec >= 60 || *fr >= 75) return std::nullopt;
    return (*m * 60 + *sec) * 75 + *fr;
}

[[nodiscard]] Ex<Cue> parse_cue(std::string_view text) {
    Cue c;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const Fields fl = fields(text.substr(0, nl));
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        const std::string_view kw = fl.f[0];
        if (svc::xml::iequal(kw, "FILE")) {
            std::string_view name;
            std::string_view type;
            if (!fl.rest.empty() && fl.rest.front() == '"') {
                const std::size_t q = fl.rest.find('"', 1);
                if (q == std::string_view::npos)
                    return std::unexpected(refuse(CR::Disk, ERR_SITE()));
                name = fl.rest.substr(1, q - 1);
                type = fields(fl.rest.substr(q + 1)).f[0];
            } else {
                name = fl.f[1];
                type = fl.f[2];
            }
            if (name.empty() || !svc::xml::iequal(type, "BINARY") || c.files.size() == 99)
                return std::unexpected(refuse(CR::Disk, ERR_SITE()));
            c.files.emplace_back(name);
        } else if (svc::xml::iequal(kw, "TRACK")) {
            const auto n = decimal(fl.f[1], 2);
            const std::string_view type = fl.f[2];
            if (c.files.empty() || !n || *n != c.tracks.size() + 1)
                return std::unexpected(refuse(CR::Disk, ERR_SITE()));
            CueTrack t{.number = static_cast<std::uint8_t>(*n),
                       .file = static_cast<std::uint16_t>(c.files.size() - 1)};
            if (svc::xml::iequal(type, "MODE2/2352") || svc::xml::iequal(type, "MODE1/2352")) {
                t.data = true;
                t.mode2 = svc::xml::iequal(type, "MODE2/2352");
            } else if (!svc::xml::iequal(type, "AUDIO")) {
                return std::unexpected(refuse(CR::Disk, ERR_SITE()));
            }
            c.tracks.push_back(t);
        } else if (svc::xml::iequal(kw, "INDEX")) {
            const auto n = decimal(fl.f[1], 2);
            const auto at = msf(fl.f[2]);
            if (c.tracks.empty() || !n || !at) return std::unexpected(refuse(CR::Disk, ERR_SITE()));
            CueTrack& t = c.tracks.back();

            if (*n == 1 && t.file == c.files.size() - 1)
                t.index1 = *at;
            else if (*n == 1)
                return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        } else if (svc::xml::iequal(kw, "FLAGS")) {
            if (c.tracks.empty()) return std::unexpected(refuse(CR::Disk, ERR_SITE()));
            std::uint8_t& fl_bits = c.tracks.back().flags;
            for (std::size_t i = 1; i < fl.f.size() && !fl.f[i].empty(); ++i) {
                const std::string_view f = fl.f[i];
                if (svc::xml::iequal(f, "PRE")) {
                    fl_bits |= 0x1u;
                } else if (svc::xml::iequal(f, "DCP")) {
                    fl_bits |= 0x2u;
                } else if (svc::xml::iequal(f, "4CH")) {
                    fl_bits |= 0x8u;
                } else if (!svc::xml::iequal(f, "SCMS")) {
                    return std::unexpected(refuse(CR::Disk, ERR_SITE()));
                }
            }
        } else if (svc::xml::iequal(kw, "PREGAP") || svc::xml::iequal(kw, "POSTGAP")) {
            return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        }
    }
    if (c.tracks.empty() || c.tracks.size() > 99)
        return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    for (const CueTrack& t : c.tracks)
        if (!t.index1) return std::unexpected(refuse(CR::Disk, ERR_SITE()));

    const CueTrack& t1 = c.tracks.front();
    if (!t1.data || t1.file != 0 || *t1.index1 != 0)
        return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    return c;
}

std::array<std::uint8_t, 12> le32x3(std::int64_t a, std::int64_t b, std::int64_t c) {
    std::array<std::uint8_t, 12> out{};
    const std::array<std::int64_t, 3> v{a, b, c};
    for (std::size_t k = 0; k < 3; ++k)
        for (unsigned i = 0; i < 4; ++i)
            out[4 * k + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v[k]) >> (8 * i));
    return out;
}

struct TocTrack {
    std::uint8_t number = 0;
    bool data = false;
    bool mode2 = false;
    std::uint8_t flags = 0;
    std::int64_t lba = 0;
};

std::vector<std::uint8_t> toc_bytes(std::span<const TocTrack> tracks, std::int64_t lead_out) {
    std::vector<std::uint8_t> out;
    out.reserve(PsxMovieCodec::kTocPrefixBytes);
    const auto add = [&out](const std::array<std::uint8_t, 12>& e) {
        out.insert(out.end(), e.begin(), e.end());
    };

    const bool xa =
        std::any_of(tracks.begin(), tracks.end(), [](const TocTrack& t) { return t.mode2; });
    add(le32x3(xa ? PsxMovieCodec::kSessionFormat : 0, tracks.front().number,
               tracks.back().number));
    for (std::size_t i = 1; i <= 100; ++i) {
        if (i <= tracks.size()) {
            const TocTrack& t = tracks[i - 1];
            add(le32x3((t.data ? 4 : 0) | t.flags, 1, t.lba));
        } else if (i == 100) {
            add(le32x3(0, 1, lead_out));
        } else {
            add(le32x3(0, 0, 0));
        }
    }
    return out;
}

std::string_view dir_of(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash + 1);
}

struct SettingRule {
    std::uint8_t lo;
    std::uint8_t width;
    std::uint8_t want;
    std::string_view name;
};
constexpr std::array<SettingRule, 22> kRules{{
    {45, 4, 2, "Pad1"},
    {49, 4, 1, "Pad2"},
    {56, 2, 0, "Multitap"},
    {16, 1, 0, "Fastboot"},
    {42, 1, 0, "CD Lid"},
    {64, 1, 1, "Pause when OSD open"},
    {72, 1, 0, "Pause when CD slow"},
    {21, 1, 0, "CD Fast Seek"},
    {75, 3, 0, "CD Speed"},
    {78, 1, 0, "Limit Max CD Speed"},
    {79, 2, 0, "Turbo"},
    {15, 1, 0, "PAL 60Hz Hack"},
    {85, 1, 0, "RAM(Homebrew)"},
    {90, 1, 0, "GPU Slowdown"},
    {89, 1, 0, "480i to 480p Hack"},
    {53, 2, 0, "Widescreen Hack"},
    {88, 1, 0, "Fast CD DMA Timing"},
    {92, 1, 0, "Old GPU(CXD8514Q)"},
    {43, 1, 0, "RepTimingSPUDMA"},
    {44, 1, 0, "SPU RAM select"},
    {63, 1, 1, "Automount Memory Card 1"},
    {71, 1, 1, "Save to SDCard"},
}};
constexpr std::uint8_t kSystemTypeLo = 39;
constexpr std::uint64_t kCueMax = 64 * 1024;
constexpr WideIoIndex kBiosDest{0xC0};

}  // namespace

Layout Layout::of(std::uint16_t b) noexcept {
    Layout l{};
    l.emu = static_cast<Emu>(b & 0x3u);
    l.firmware_region = static_cast<std::uint8_t>((b >> 2) & 0x3u);
    l.platform = ((b >> 4) & 1u) != 0;
    l.log_key = ((b >> 5) & 1u) != 0;
    l.pad_digital = ((b >> 6) & 1u) != 0;
    l.card_off = ((b >> 7) & 1u) != 0;
    return l;
}

std::uint16_t Layout::bits() const noexcept {
    return static_cast<std::uint16_t>(static_cast<unsigned>(emu) |
                                      unsigned{firmware_region & 0x3u} << 2 |
                                      unsigned{platform} << 4 | unsigned{log_key} << 5 |
                                      unsigned{pad_digital} << 6 | unsigned{card_off} << 7);
}

const MovieArchiveFormat* PsxMovieCodec::archive() const noexcept { return &kBk2Archive; }

bool PsxMovieCodec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

bool PsxMovieCodec::ends_log(std::string_view line) const noexcept {
    return strip(line) == "[/Input]";
}

Ex<IMovieCodec::Facts> PsxMovieCodec::header_line(std::string_view line,
                                                  const Facts& so_far) const noexcept {
    Facts f = so_far;
    Layout l = Layout::of(f.layout);
    line = strip(line);
    if (line.empty()) return f;
    if (line == "@Core" || line == "@CoreText" || line == "@SaveRam" ||
        line.starts_with("SavestateBinaryBase64Blob")) {
        return std::unexpected(refuse(CR::Savestate, ERR_SITE()));
    }
    if (line.starts_with("LogKey:")) {
        const auto k = read_log_key(line.substr(7), f);
        if (!k) return std::unexpected(refuse(CR::PortType, ERR_SITE()));
        f = *k;
        f.ports = 0x1;
        l.log_key = true;
        f.layout = l.bits();
        return f;
    }
    if (line.size() > kLineMax) return f;
    const Fields kv = fields(line);
    const std::string_view key = kv.f[0];
    const std::string_view val = kv.rest;
    if (key == "MovieVersion") {
        if (!val.starts_with("BizHawk v2"))
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        f.version = 2;
    } else if (key == "Platform") {
        if (val != "PSX") return std::unexpected(refuse(CR::System, ERR_SITE()));
        l.platform = true;
    } else if (key == "Core") {

        if (val == "Octoshock") {
            l.emu = Emu::Octoshock;
        } else if (val == "Nymashock") {
            l.emu = Emu::Nymashock;
        } else {
            return std::unexpected(refuse(CR::System, ERR_SITE()));
        }
    } else if (key == "StartsFromSavestate" || key == "StartsFromSaveRam") {
        const auto b = boolean(val);
        if (!b) return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        if (*b) return std::unexpected(refuse(CR::Savestate, ERR_SITE()));
    } else if (key == "SHA1") {

        const auto d = parse_hex_digest(val, DigestKind::Crc32);
        if (!d) return std::unexpected(refuse(CR::Checksum, ERR_SITE()));
        f.digest = *d;
        f.has_digest = true;
    } else if (key.starts_with("PSX_Firmware_")) {
        static constexpr std::string_view kRegions = "UJE";
        const std::string_view r = key.substr(13);
        const std::size_t code = r.size() == 1 ? kRegions.find(r.front()) : std::string_view::npos;
        const auto d = parse_hex_digest(val, DigestKind::Sha1);
        if (code == std::string_view::npos || !d || f.has_firmware)
            return std::unexpected(refuse(CR::Checksum, ERR_SITE()));
        f.firmware = *d;
        f.has_firmware = true;
        l.firmware_region = static_cast<std::uint8_t>(code + 1);
        f.region = r == "E" ? Region::Pal : Region::Ntsc;
    } else if (key.starts_with("SyncSettings.")) {
        std::string_view k = key.substr(13);
        if (k.starts_with("o.")) k.remove_prefix(2);
        const auto index = [](std::string_view tail) { return decimal(tail, 1); };
        if (k.starts_with("FIOConfig.Devices8.")) {

            const auto n = index(k.substr(19));
            const auto v = decimal(val, 2);
            if (!n || !v || *v != (*n == 0 ? 1u : 0u))
                return std::unexpected(refuse(CR::PortType, ERR_SITE()));
            if (*n == 0) l.pad_digital = true;
        } else if (k.starts_with("FIOConfig.Memcards.")) {
            const auto n = index(k.substr(19));
            const auto b = boolean(val);
            if (!n || !b) return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
            if (*b) return std::unexpected(refuse(CR::Setting, ERR_SITE()));
            if (*n == 0) l.card_off = true;
        } else if (k.starts_with("FIOConfig.Multitaps.")) {
            const auto b = boolean(val);
            if (!b) return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
            if (*b) return std::unexpected(refuse(CR::Multitap, ERR_SITE()));
        } else if (k.starts_with("PortDevices.")) {

            const auto n = index(k.substr(12));
            if (!n) return std::unexpected(refuse(CR::PortType, ERR_SITE()));
            if (!svc::xml::iequal(val, *n == 0 ? "gamepad" : "none"))
                return std::unexpected(refuse(CR::PortType, ERR_SITE()));
            if (*n == 0) l.pad_digital = true;
        } else if (k == "MednafenValues.psx.input.port1.memcard") {

            if (val != "0") return std::unexpected(refuse(CR::Setting, ERR_SITE()));
            l.card_off = true;
        } else if (k.starts_with("MednafenValues.psx.input.pport") && k.ends_with(".multitap")) {
            if (val != "0") return std::unexpected(refuse(CR::Multitap, ERR_SITE()));
        }
    }
    note_rerecords(key, val, f);
    f.layout = l.bits();
    return f;
}

Ex<void> PsxMovieCodec::finish_header(const Facts& f) const noexcept {
    const Layout l = Layout::of(f.layout);
    if (f.version != 2 || !l.platform || l.emu == Emu::None)
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
    if (!l.log_key || !l.pad_digital) return std::unexpected(refuse(CR::PortType, ERR_SITE()));
    if (!l.card_off) return std::unexpected(refuse(CR::Setting, ERR_SITE()));
    if (!f.has_digest || !f.has_firmware) return std::unexpected(refuse(CR::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> PsxMovieCodec::frame(std::string_view line, const Facts& f) const noexcept {
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kLineMax || !starts_log(line))
        return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
    Frame out{};
    std::size_t at = 0;
    for (std::size_t c = 0; c < f.columns_n; ++c) {
        while (at < line.size() && line[at] == '|')
            ++at;
        if (at == line.size()) return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
        const std::uint8_t role = f.columns[c];
        if (role == kColValue) {

            const std::size_t comma = line.find(',', at);
            if (comma == std::string_view::npos)
                return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
            if (!decimal(strip(line.substr(at, comma - at)), 3))
                return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
            at = comma + 1;
            continue;
        }
        const auto ch = static_cast<unsigned char>(line[at++]);
        if (ch == '.') continue;

        if (ch <= ' ' || ch >= 0x7F) return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
        if (role == kColTray) return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        if (role == kColReset) {
            out.commands = static_cast<std::uint8_t>(out.commands | 0x1u);
        } else if (role == kColPower) {
            out.commands = static_cast<std::uint8_t>(out.commands | 0x2u);
        } else {
            out.mask[0] |= 1u << role;
        }
    }
    for (; at < line.size(); ++at)
        if (line[at] != '|') return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
    return out;
}

IMovieCodec::SettingNeeds PsxMovieCodec::setting_needs(const Facts& f) const noexcept {
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add({.lo = r.lo,
                 .width = r.width,
                 .allowed = static_cast<std::uint16_t>(1u << r.want),
                 .preferred = r.want,
                 .name = r.name});
    }

    const auto region = static_cast<std::uint8_t>(Layout::of(f.layout).firmware_region);
    out.add({.lo = kSystemTypeLo,
             .width = 2,
             .allowed = static_cast<std::uint16_t>(1u << region),
             .preferred = region,
             .name = "System Type"});
    return out;
}

IMovieCodec::Raster PsxMovieCodec::raster(const Facts& f) const noexcept {

    if (f.region == Region::Pal) return {.period_ns = 20'101'789, .line0_ns = 4'609'327};
    return {.period_ns = 16'717'562, .line0_ns = 1'334'862};
}

IMovieCodec::PowerOn PsxMovieCodec::power_on(const Facts&) const noexcept {
    return {.clocks = 0, .p0 = Parity::AfterSilence, .event = PowerOnEvent::ResetPulse};
}

std::int32_t PsxMovieCodec::default_lead(const Facts&) const noexcept { return 0; }

std::optional<IMovieCodec::DigestSpan> PsxMovieCodec::rom_digest_span(
    std::span<const std::uint8_t>, std::uint64_t) const noexcept {
    return std::nullopt;
}

Ex<std::vector<std::uint8_t>> PsxMovieCodec::toc_prefix(std::string_view cue,
                                                        std::span<const std::uint64_t> file_sizes) {
    const auto c = parse_cue(cue);
    if (!c) return std::unexpected(c.error());
    if (file_sizes.size() != c->files.size()) return std::unexpected(refuse(CR::Disk, ERR_SITE()));

    std::vector<std::int64_t> base(c->files.size() + 1, 0);
    for (std::size_t i = 0; i < c->files.size(); ++i)
        base[i + 1] = base[i] + static_cast<std::int64_t>(file_sizes[i] / kSectorBytes);
    if (file_sizes.front() < kKeySectors * kSectorBytes)
        return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    std::vector<TocTrack> toc;
    toc.reserve(c->tracks.size());
    for (const CueTrack& t : c->tracks)
        toc.push_back({.number = t.number,
                       .data = t.data,
                       .mode2 = t.mode2,
                       .flags = t.flags,
                       .lba = base[t.file] + *t.index1});
    return toc_bytes(toc, base.back());
}

Ex<std::vector<std::uint8_t>> PsxMovieCodec::chd_key(svc::IChdSource& chd) {

    std::vector<TocTrack> toc;
    std::int64_t lba = 0;
    std::uint32_t first_frames = 0;
    for (std::uint32_t i = 0;; ++i) {
        const auto row = chd.track_metadata(i);
        if (!row) break;
        const auto t = svc::parse_chd_track(*row);
        if (!t || t->number != i + 1 || i == 99)
            return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        const std::string_view type(t->type);
        TocTrack e{.number = static_cast<std::uint8_t>(t->number)};
        if (type == "MODE2_RAW" || type == "MODE1_RAW") {
            e.data = true;
            e.mode2 = type == "MODE2_RAW";
        } else if (type != "AUDIO") {
            return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        }

        const bool held = t->pgtype[0] == 'V';
        if ((t->pregap != 0 && !held) || t->postgap != 0)
            return std::unexpected(refuse(CR::Disk, ERR_SITE()));
        e.lba = lba + t->pregap;
        lba += t->frames;
        if (i == 0) first_frames = t->frames;
        toc.push_back(e);
    }
    if (toc.empty() || !toc.front().data || toc.front().lba != 0 || first_frames < kKeySectors)
        return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    std::vector<std::uint8_t> out = toc_bytes(toc, lba);

    const svc::IChdSource::Geometry geo = chd.geometry();
    if (geo.unit_bytes != kChdFrameBytes || geo.hunk_bytes < geo.unit_bytes ||
        geo.hunk_bytes > kChdHunkMax)
        return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    const std::uint32_t per_hunk = geo.hunk_bytes / geo.unit_bytes;
    std::vector<std::byte> hunk(geo.hunk_bytes);
    std::optional<std::uint32_t> held_hunk{};
    out.reserve(out.size() + kKeySectors * kSectorBytes);
    for (std::uint32_t f = 0; f < kKeySectors; ++f) {
        if (held_hunk != f / per_hunk) {
            if (auto r = chd.read_hunk(f / per_hunk, hunk); !r) return std::unexpected(r.error());
            held_hunk = f / per_hunk;
        }
        const auto* p = reinterpret_cast<const std::uint8_t*>(hunk.data()) +
                        static_cast<std::size_t>(f % per_hunk) * geo.unit_bytes;
        out.insert(out.end(), p, p + kSectorBytes);
    }
    return out;
}

Error PsxMovieCodec::chd_open_error(const Error& e) noexcept {
    if (e.code == Errc::mount_failed) return refuse(CR::ChdUnsupported, ERR_SITE());
    if (e.code == Errc::bad_format) return refuse(CR::Disk, ERR_SITE());
    return e;
}

namespace {

[[nodiscard]] Ex<IMovieCodec::DigestSource> chd_digest_source(const svc::Vfs& vfs,
                                                              std::string_view rom) {
    auto f = vfs.open(rom, svc::OpenMode::Read);
    if (!f) return std::unexpected(f.error());
    auto chd = svc::open_chd(std::move(*f));
    if (!chd) return std::unexpected(PsxMovieCodec::chd_open_error(chd.error()));
    auto key = PsxMovieCodec::chd_key(**chd);
    if (!key) return std::unexpected(key.error());
    return IMovieCodec::DigestSource{
        .file = std::string(rom), .span = {.offset = 0, .length = 0}, .prefix = std::move(*key)};
}

}  // namespace

Ex<IMovieCodec::DigestSource> PsxMovieCodec::digest_source(const svc::Vfs& vfs,
                                                           std::string_view rom) const {
    const std::string_view ext = rom.size() < 4 ? std::string_view{} : rom.substr(rom.size() - 4);
    if (svc::xml::iequal(ext, ".chd")) return chd_digest_source(vfs, rom);
    if (!svc::xml::iequal(ext, ".cue")) return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    auto cue = vfs.open(rom, svc::OpenMode::Read);
    if (!cue) return std::unexpected(cue.error());
    const auto size = (*cue)->size();
    if (!size) return std::unexpected(size.error());
    if (size->v > kCueMax) return std::unexpected(refuse(CR::Disk, ERR_SITE()));
    std::string text(static_cast<std::size_t>(size->v), '\0');
    const auto got = (*cue)->read_at(0, std::as_writable_bytes(std::span<char>(text)));
    if (!got) return std::unexpected(got.error());
    text.resize(*got);
    const auto c = parse_cue(text);
    if (!c) return std::unexpected(c.error());
    const std::string dir(dir_of(rom));
    std::vector<std::uint64_t> sizes;
    for (const std::string& name : c->files) {
        auto f = vfs.open(dir + name, svc::OpenMode::Read);
        if (!f) return std::unexpected(f.error());
        const auto s = (*f)->size();
        if (!s) return std::unexpected(s.error());
        sizes.push_back(s->v);
    }
    auto prefix = toc_prefix(text, sizes);
    if (!prefix) return std::unexpected(prefix.error());
    return DigestSource{.file = dir + c->files.front(),
                        .span = {.offset = 0, .length = kKeySectors * kSectorBytes},
                        .prefix = std::move(*prefix)};
}

std::optional<IMovieCodec::Companion> PsxMovieCodec::companion(std::string_view rom,
                                                               const Facts& f) const {
    if (!f.has_firmware) return std::nullopt;

    const std::string_view dir = dir_of(rom);
    const std::string_view up = dir.empty() ? dir : dir.substr(0, dir.size() - 1);
    Companion c{};
    std::size_t n = 0;
    for (const BootAsset& row : manifests::kPsxBootAssets) {
        if (n == c.paths.size() || row.dest != kBiosDest) continue;
        c.size = row.exact_size;
        if (row.anchor == AssetAnchor::ImageDir && !dir.empty()) {
            c.paths[n++] = std::string(dir) + std::string(row.name);
        } else if (row.anchor == AssetAnchor::ImageParentDir &&
                   up.find('/') != std::string_view::npos) {
            c.paths[n++] = std::string(dir_of(up)) + std::string(row.name);
        }
    }
    if (n == 0) return std::nullopt;
    return c;
}

}  // namespace mister::cores
