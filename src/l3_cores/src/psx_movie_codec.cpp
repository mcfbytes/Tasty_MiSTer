// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_movie_codec.h"
#include "cores/bk2_log.h"
#include "cores/bk2_text.h"
#include "cores/movie_codec.h"
#include "cores/movie_refusal.h"
#include "cores/psx_bk2_format.h"

#include <string>
#include <utility>
#include <vector>

#include "cores/manifests/psx.h"
#include "cores/movie_archive.h"
#include "cores/psx_disc_key.h"
#include "infra/ascii.h"
#include "infra/parse_int.h"
#include "infra/path_parts.h"
#include "svc/chd_source.h"
#include "svc/file.h"
#include "svc/types.h"
#include "svc/vfs.h"

namespace mister::cores {

using infra::ieq;

namespace {

using CR = MovieRefusal;
using MovieEmu = PsxMovieCodec::Emu;
using Layout = PsxMovieCodec::Layout;
using FirmwareRegion = PsxMovieCodec::FirmwareRegion;

constexpr std::array<FirmwareRegion, 4> kRegionByCode{
    FirmwareRegion::None,
    FirmwareRegion::Usa,
    FirmwareRegion::Japan,
    FirmwareRegion::Europe
};

std::optional<Bk2Column> column_of(std::string_view name) {
    if (name == "Disc Select" || name == "Disk Index")
        return Bk2Column::disc_index();
    if (name == "Open" || name == "Close" || name == "Open Tray" || name == "Close Tray")
        return Bk2Column::tray();
    if (name == "Reset")
        return Bk2Column::reset();
    if (name == "Power")
        return Bk2Column::power();
    const auto pad = bk2_pad_name(name);

    if (!pad || pad->port != svc::kPlayers.front())
        return std::nullopt;
    for (const PsxMovieCodec::Button& b : PsxMovieCodec::kButtons) {
        if (pad->name == b.octoshock || pad->name == b.nymashock)
            return Bk2Column::pad(svc::kPlayers.front(), b.bit);
    }
    return std::nullopt;
}

std::optional<MovieFacts> read_log_key(std::string_view key, MovieFacts f) {
    f.columns = Bk2Columns{};
    proto::JoyMask bits{};
    while (!key.empty()) {
        const std::size_t end = key.find_first_of("|#");
        const std::string_view name = key.substr(0, end);
        key.remove_prefix(end == std::string_view::npos ? key.size() : end + 1);
        if (name.empty())
            continue;
        const auto c = column_of(name);
        if (!c)
            return std::nullopt;
        if (const auto cell = c->pad_cell()) {
            if (bits.any(cell->bit.mask()))
                return std::nullopt;
            bits = bits.with(cell->bit.mask());
        }
        const auto next = f.columns.append(*c);
        if (!next)
            return std::nullopt;
        f.columns = *next;
    }
    if (bits != PsxMovieCodec::kPadBitMask)
        return std::nullopt;
    return f;
}

struct SettingRule {
    proto::BitField field;
    std::uint8_t want;
    std::string_view name;
};

constexpr std::array<SettingRule, 22> kRules{{
    {{proto::StatusBit{45}, 4}, 2, "Pad1"},
    {{proto::StatusBit{49}, 4}, 1, "Pad2"},
    {{proto::StatusBit{56}, 2}, 0, "Multitap"},
    {{proto::StatusBit{16}, 1}, 0, "Fastboot"},
    {{proto::StatusBit{42}, 1}, 0, "CD Lid"},
    {{proto::StatusBit{64}, 1}, 1, "Pause when OSD open"},
    {{proto::StatusBit{72}, 1}, 0, "Pause when CD slow"},
    {{proto::StatusBit{21}, 1}, 0, "CD Fast Seek"},
    {{proto::StatusBit{75}, 3}, 0, "CD Speed"},
    {{proto::StatusBit{78}, 1}, 0, "Limit Max CD Speed"},
    {{proto::StatusBit{79}, 2}, 0, "Turbo"},
    {{proto::StatusBit{15}, 1}, 0, "PAL 60Hz Hack"},
    {{proto::StatusBit{85}, 1}, 0, "RAM(Homebrew)"},
    {{proto::StatusBit{90}, 1}, 0, "GPU Slowdown"},
    {{proto::StatusBit{89}, 1}, 0, "480i to 480p Hack"},
    {{proto::StatusBit{53}, 2}, 0, "Widescreen Hack"},
    {{proto::StatusBit{88}, 1}, 0, "Fast CD DMA Timing"},
    {{proto::StatusBit{92}, 1}, 0, "Old GPU(CXD8514Q)"},
    {{proto::StatusBit{43}, 1}, 0, "RepTimingSPUDMA"},
    {{proto::StatusBit{44}, 1}, 0, "SPU RAM select"},
    {{proto::StatusBit{63}, 1}, 1, "Automount Memory Card 1"},
    {{proto::StatusBit{71}, 1}, 1, "Save to SDCard"},
}};

constexpr proto::BitField kSystemTypeField{proto::StatusBit{39}, 2};
constexpr std::uint64_t kCueMax = 64 * 1024;
constexpr WideIoIndex kBiosDest{0xC0};

}

namespace {

enum class LayoutField : std::uint8_t {
    Emu,
    Region,
    Platform,
    LogKey,
    PadDigital,
    CardOff,
    kCount
};

struct LayoutBits {
    std::uint8_t shift;
    std::uint8_t mask;
};

constexpr std::array<LayoutBits, static_cast<std::size_t>(LayoutField::kCount)> kLayoutBits{{
    {0, 0x3},
    {2, 0x3},
    {4, 0x1},
    {5, 0x1},
    {6, 0x1},
    {7, 0x1},
}};

constexpr unsigned field_of(std::uint16_t bits, LayoutField f) noexcept {
    const LayoutBits& row = kLayoutBits[static_cast<std::size_t>(f)];
    return static_cast<unsigned>((bits >> row.shift) & row.mask);
}

constexpr unsigned pack_field(LayoutField f, unsigned value) noexcept {
    const LayoutBits& row = kLayoutBits[static_cast<std::size_t>(f)];
    return (value & row.mask) << row.shift;
}

}

Layout Layout::of(std::uint16_t b) noexcept {
    Layout l{};
    l.emu = static_cast<MovieEmu>(field_of(b, LayoutField::Emu));
    l.firmware_region = kRegionByCode[field_of(b, LayoutField::Region)];
    l.platform = field_of(b, LayoutField::Platform) != 0;
    l.log_key = field_of(b, LayoutField::LogKey) != 0;
    l.pad_digital = field_of(b, LayoutField::PadDigital) != 0;
    l.card_off = field_of(b, LayoutField::CardOff) != 0;
    return l;
}

std::uint16_t Layout::bits() const noexcept {
    return static_cast<std::uint16_t>(
        pack_field(LayoutField::Emu, static_cast<unsigned>(emu))
        | pack_field(LayoutField::Region, std::to_underlying(firmware_region))
        | pack_field(LayoutField::Platform, unsigned{platform})
        | pack_field(LayoutField::LogKey, unsigned{log_key})
        | pack_field(LayoutField::PadDigital, unsigned{pad_digital})
        | pack_field(LayoutField::CardOff, unsigned{card_off})
    );
}

infra::OptRef<const MovieArchiveFormat> PsxBk2Format::archive() const noexcept {
    return infra::OptRef<const MovieArchiveFormat>{kBk2Archive};
}

bool PsxBk2Format::starts_log(std::string_view line) const noexcept {
    return bk2_starts_log(line);
}

bool PsxBk2Format::ends_log(std::string_view line) const noexcept {
    return bk2_ends_log(line);
}

Ex<MovieFacts> PsxBk2Format::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    MovieFacts f = so_far;
    Layout l = f.layout.view_as<Layout>();
    const auto read = bk2_read_line(line);
    if (!read)
        return std::unexpected(read.error());
    const Bk2Line& ln = *read;
    switch (ln.kind) {
        case Bk2Line::Kind::Blank:
        case Bk2Line::Kind::Long:
            return f;
        case Bk2Line::Kind::LogKey: {
            const auto k = read_log_key(ln.value, f);
            if (!k)
                return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
            f = *k;
            f.ports = svc::PlayerMask{}.with(svc::kPlayers[0]);
            l.log_key = true;
            f.layout = FormatLayout::of(l.bits());
            return f;
        }
        case Bk2Line::Kind::Pair:
            break;
    }
    const std::string_view key = ln.key;
    const std::string_view val = ln.value;
    if (key == "MovieVersion") {
        if (!bk2_version_ok(val))
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        f.version = MovieRevision{2};
    } else if (key == "Platform") {
        if (val != kPlatformName)
            return std::unexpected(movie_refusal(CR::System, ERR_SITE()));
        l.platform = true;
    } else if (key == "Core") {

        if (val == "Octoshock") {
            l.emu = MovieEmu::Octoshock;
        } else if (val == "Nymashock") {
            l.emu = MovieEmu::Nymashock;
        } else {
            return std::unexpected(movie_refusal(CR::System, ERR_SITE()));
        }
    } else if (key == "SHA1") {

        const auto d = parse_hex_digest(val, DigestKind::Crc32);
        if (!d)
            return std::unexpected(movie_refusal(CR::Checksum, ERR_SITE()));
        f.digest = *d;
    } else if (key.starts_with("PSX_Firmware_")) {
        static constexpr std::string_view kRegions = "UJE";
        const std::string_view r = key.substr(13);
        const std::size_t code = r.size() == 1 ? kRegions.find(r.front()) : std::string_view::npos;
        const auto d = parse_hex_digest(val, DigestKind::Sha1);
        if (code == std::string_view::npos || !d || f.firmware)
            return std::unexpected(movie_refusal(CR::Checksum, ERR_SITE()));
        f.firmware = *d;
        l.firmware_region = kRegionByCode[code + 1];
        f.region = r == "E" ? MovieRegion::Pal : MovieRegion::Ntsc;
    } else if (key.starts_with("SyncSettings.")) {
        std::string_view k = key.substr(13);
        if (k.starts_with("o."))
            k.remove_prefix(2);
        const auto index = [](std::string_view tail) {
            return tail.size() <= 1 ? infra::parse_int<std::uint32_t>(tail) : std::nullopt;
        };
        if (k.starts_with("FIOConfig.Devices8.")) {

            const auto n = index(k.substr(19));
            const auto v = val.size() <= 2 ? infra::parse_int<std::uint32_t>(val) : std::nullopt;
            if (!n || !v || *v != (*n == 0 ? 1u : 0u))
                return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
            if (*n == 0)
                l.pad_digital = true;
        } else if (k.starts_with("FIOConfig.Memcards.")) {
            const auto n = index(k.substr(19));
            const auto b = bk2_boolean(val);
            if (!n || !b)
                return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
            if (*b)
                return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
            if (*n == 0)
                l.card_off = true;
        } else if (k.starts_with("FIOConfig.Multitaps.")) {
            const auto b = bk2_boolean(val);
            if (!b)
                return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
            if (*b)
                return std::unexpected(movie_refusal(CR::Multitap, ERR_SITE()));
        } else if (k.starts_with("PortDevices.")) {

            const auto n = index(k.substr(12));
            if (!n)
                return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
            if (!ieq(val, *n == 0 ? "gamepad" : "none"))
                return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
            if (*n == 0)
                l.pad_digital = true;
        } else if (k == "MednafenValues.psx.input.port1.memcard") {

            if (val != "0")
                return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
            l.card_off = true;
        } else if (k.starts_with("MednafenValues.psx.input.pport") && k.ends_with(".multitap")) {
            if (val != "0")
                return std::unexpected(movie_refusal(CR::Multitap, ERR_SITE()));
        }
    }
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    f.layout = FormatLayout::of(l.bits());
    return f;
}

Ex<void> PsxBk2Format::finish_header(const MovieFacts& f) const noexcept {
    const Layout l = f.layout.view_as<Layout>();
    if (f.version != MovieRevision{2} || !l.platform || l.emu == MovieEmu::None)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    if (!l.log_key || !l.pad_digital)
        return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
    if (!l.card_off)
        return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
    if (!f.digest || !f.firmware)
        return std::unexpected(movie_refusal(CR::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> PsxBk2Format::frame(std::string_view line, const MovieFacts& f) const noexcept {
    return bk2_decode_flat(line, f);
}

IReplayCore::SettingNeeds PsxMovieCodec::setting_needs(const MovieFacts& f) const noexcept {
    SettingNeeds out{};
    for (const SettingRule& r : kRules) {
        out.add(
            {.field = r.field,
             .allowed = proto::ValueSet{static_cast<std::uint16_t>(1u << r.want)},
             .preferred = proto::FieldValue{r.want},
             .name = r.name}
        );
    }

    const auto region = std::to_underlying(f.layout.view_as<Layout>().firmware_region);
    out.add(
        {.field = kSystemTypeField,
         .allowed = proto::ValueSet{static_cast<std::uint16_t>(1u << region)},
         .preferred = proto::FieldValue{region},
         .name = "System Type"}
    );
    return out;
}

IReplayCore::Raster PsxMovieCodec::raster(const MovieFacts& f) const noexcept {

    if (f.region == MovieRegion::Pal)
        return {
            .period_ns = std::chrono::nanoseconds{20'101'789},
            .line0_ns = std::chrono::nanoseconds{4'609'327}
        };
    return {
        .period_ns = std::chrono::nanoseconds{16'717'562},
        .line0_ns = std::chrono::nanoseconds{1'334'862}
    };
}

IReplayCore::PowerOn PsxMovieCodec::power_on(const MovieFacts&) const noexcept {
    return {.p0 = Parity::AfterSilence, .event = PowerOnEvent::ResetPulse};
}

namespace {

[[nodiscard]] Ex<IMovieFormat::DigestSource> chd_digest_source(
    const svc::Vfs& vfs,
    std::string_view rom
) {
    auto f = vfs.open(rom, svc::OpenMode::Read);
    if (!f)
        return std::unexpected(f.error());
    auto chd = svc::open_chd(std::move(*f));
    if (!chd)
        return std::unexpected(PsxDiscKey::chd_open_error(chd.error()));
    auto key = PsxDiscKey::chd_key(**chd);
    if (!key)
        return std::unexpected(key.error());
    return IMovieFormat::DigestSource{
        .file = std::string(rom),
        .span = {},
        .prefix = std::move(*key)
    };
}

}

Ex<IMovieFormat::DigestSource> PsxBk2Format::digest_source(
    const svc::Vfs& vfs,
    std::string_view rom
) const {
    const std::string_view ext = rom.size() < 4 ? std::string_view{} : rom.substr(rom.size() - 4);
    if (ieq(ext, ".chd"))
        return chd_digest_source(vfs, rom);
    if (!ieq(ext, ".cue"))
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    auto cue = vfs.open(rom, svc::OpenMode::Read);
    if (!cue)
        return std::unexpected(cue.error());
    const auto size = (*cue)->size();
    if (!size)
        return std::unexpected(size.error());
    if (*size > proto::FileSize{kCueMax})
        return std::unexpected(movie_refusal(CR::Disk, ERR_SITE()));
    std::string text(static_cast<std::size_t>(size->v), '\0');
    const auto got = (*cue)->read_at(0, std::as_writable_bytes(std::span<char>(text)));
    if (!got)
        return std::unexpected(got.error());
    text.resize(*got);
    const auto files = PsxDiscKey::cue_files(text);
    if (!files)
        return std::unexpected(files.error());
    const std::string dir(infra::dir_prefix(rom));
    std::vector<proto::FileSize> sizes;
    for (const std::string& name : *files) {
        auto f = vfs.open(dir + name, svc::OpenMode::Read);
        if (!f)
            return std::unexpected(f.error());
        const auto s = (*f)->size();
        if (!s)
            return std::unexpected(s.error());
        sizes.push_back(*s);
    }
    auto prefix = PsxDiscKey::toc_prefix(text, sizes);
    if (!prefix)
        return std::unexpected(prefix.error());
    return DigestSource{
        .file = dir + files->front(),
        .span =
            {.offset = {},
             .length = proto::FileSize{PsxDiscKey::kKeySectors * PsxDiscKey::kSectorBytes}},
        .prefix = std::move(*prefix)
    };
}

std::optional<IReplayCore::Companion> PsxMovieCodec::companion(
    std::string_view rom,
    const MovieFacts& f
) const {
    if (!f.firmware)
        return std::nullopt;

    const std::string_view dir = infra::dir_prefix(rom);
    const std::string_view up = dir.empty() ? dir : dir.substr(0, dir.size() - 1);
    Companion c{};
    std::size_t n = 0;
    for (const BootAsset& row : manifests::kPsxBootAssets) {
        if (n == c.paths.size() || row.dest != kBiosDest)
            continue;
        c.size = row.exact_size.value_or(proto::FileSize{});
        if (row.anchor == AssetAnchor::ImageDir && !dir.empty()) {
            c.paths[n++] = std::string(dir) + std::string(row.name);
        } else if (
            row.anchor == AssetAnchor::ImageParentDir && up.find('/') != std::string_view::npos
        ) {
            c.paths[n++] = std::string(infra::dir_prefix(up)) + std::string(row.name);
        }
    }
    if (n == 0)
        return std::nullopt;
    return c;
}

}
