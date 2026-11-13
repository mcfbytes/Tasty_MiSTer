// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megadrive_movie_codec.h"
#include "cores/movie_codec.h"
#include "cores/megadrive_bk2_format.h"
#include "cores/megadrive_gmv_format.h"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <string>
#include <utility>

#include "cores/bk2_log.h"
#include "cores/bk2_text.h"
#include "cores/movie_archive.h"
#include "cores/movie_refusal.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/parse_int.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {
namespace {

using CR = MovieRefusal;
using Pad = MegaDriveMovieCodec::Pad;
using Sys = MegaDriveMovieCodec::Sys;
using Layout = MegaDriveMovieCodec::Layout;
using GpgxRegion = MegaDriveMovieCodec::Layout::GpgxRegion;
using CoreRegion = MegaDriveMovieCodec::Layout::CoreRegion;

constexpr MovieRevision kBizHawkRevision{2};

[[nodiscard]] Ex<GpgxRegion> gpgx_region(std::string_view v) {
    constexpr std::size_t kGpgxRegionDigits = 1;
    constexpr std::size_t kJapanPal = 4;
    static constexpr std::array<std::string_view, kJapanPal + 1>
        kNames{"Autodetect", "USA", "Europe", "Japan_NTSC", "Japan_PAL"};
    static constexpr std::array<GpgxRegion, kJapanPal>
        kRegions{GpgxRegion::Auto, GpgxRegion::Usa, GpgxRegion::Europe, GpgxRegion::JapanNtsc};
    std::optional<std::size_t> code = v.size() <= kGpgxRegionDigits
        ? infra::parse_int<std::uint32_t>(v)
        : std::nullopt;
    for (std::size_t i = 0; !code && i < kNames.size(); ++i)
        if (infra::ieq(v, kNames[i]))
            code = i;
    if (!code || *code > kJapanPal)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    if (*code == kJapanPal)
        return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
    return kRegions[*code];
}

struct Group {
    Pad pad = Pad::None;
    bool reset_first = false;
};

std::optional<Group> read_group(std::string_view g, unsigned player) {
    constexpr std::size_t kPrefixBytes = 3;
    constexpr std::size_t kThreeButtonBits = 8;
    constexpr std::size_t kSixButtonBits = 12;
    constexpr std::size_t kSystemGroupBits = 2;
    std::array<std::string_view, kSixButtonBits> names{};
    std::size_t n = 0;
    while (!g.empty()) {
        const std::size_t bar = g.find('|');
        if (bar == std::string_view::npos || n == names.size())
            return std::nullopt;
        names[n++] = g.substr(0, bar);
        g.remove_prefix(bar + 1);
    }
    if (n == kSystemGroupBits
        && ((names[0] == "Power" && names[1] == "Reset")
            || (names[0] == "Reset" && names[1] == "Power"))) {
        return Group{.pad = Pad::None, .reset_first = names[0] == "Reset"};
    }
    if (n != kThreeButtonBits && n != kSixButtonBits)
        return std::nullopt;
    const char prefix[kPrefixBytes] = {'P', static_cast<char>('0' + player), ' '};
    for (std::size_t i = 0; i < n; ++i) {
        const std::string_view name = names[i];
        if (!name.starts_with(std::string_view(prefix, kPrefixBytes)))
            return std::nullopt;
        if (name.substr(kPrefixBytes) != MegaDriveBk2Format::kPadName[i])
            return std::nullopt;
    }
    return Group{.pad = n == kSixButtonBits ? Pad::Six : Pad::Three, .reset_first = false};
}

std::optional<Layout> read_log_key(std::string_view key, Layout l) {
    constexpr unsigned kMultitapFirstPlayer = 1;
    constexpr unsigned kMaxPlayers = 2;
    if (key.empty() || key.front() != '#')
        return std::nullopt;
    key.remove_prefix(1);
    l.p1 = l.p2 = Pad::None;
    l.sys = Sys::None;
    l.reset_first = false;
    unsigned group = 0, player = kMultitapFirstPlayer;
    while (true) {
        const std::size_t hash = key.find('#');
        const std::string_view g = key.substr(0, hash);
        const auto gr = read_group(g, player);
        if (!gr)
            return std::nullopt;
        if (gr->pad == Pad::None) {
            if (l.sys != Sys::None || (group != 0 && hash != std::string_view::npos))
                return std::nullopt;
            l.sys = group == 0 ? Sys::First : Sys::Last;
            l.reset_first = gr->reset_first;
        } else {
            if (player > kMaxPlayers)
                return std::nullopt;
            (player == kMultitapFirstPlayer ? l.p1 : l.p2) = gr->pad;
            ++player;
        }
        ++group;
        if (hash == std::string_view::npos)
            break;
        key.remove_prefix(hash + 1);
    }
    if (l.p1 == Pad::None)
        return std::nullopt;
    return l;
}

std::size_t width(Pad p) {
    constexpr std::size_t kThreeButtonBits = 8;
    constexpr std::size_t kSixButtonBits = 12;
    return p == Pad::Six ? kSixButtonBits : p == Pad::Three ? kThreeButtonBits : 0u;
}

using LogGroup = std::optional<Pad>;

struct SettingRule {
    proto::BitField field;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr std::array<SettingRule, 8> kRules{{
    {{proto::StatusBit{4}, 1}, 0x01, "Swap Joysticks"},
    {{proto::StatusBit{37}, 3}, 0x01, "Multitap"},
    {{proto::StatusBit{18}, 2}, 0x01, "Mouse"},
    {{proto::StatusBit{21}, 2}, 0x01, "Keyboard"},
    {{proto::StatusBit{40}, 2}, 0x01, "Gun Control"},
    {{proto::StatusBit{62}, 2}, 0x01, "SNAC"},
    {{proto::StatusBit{61}, 1}, 0x01, "Pause When OSD is Open"},
    {{proto::StatusBit{12}, 1}, 0x01, "TMSS"},
}};
constexpr proto::BitField kSixButtonField{proto::StatusBit{5}, 1};

constexpr proto::BitField kRegionField{proto::StatusBit{6}, 2};

constexpr proto::BitField kAutoRegionField{proto::StatusBit{8}, 2};

constexpr proto::BitField kPriorityField{proto::StatusBit{27}, 2};
constexpr unsigned kAutoDisabled = 2;
constexpr unsigned kUsJpEu = 2;

constexpr std::uint32_t kClkMdNtscHz = 107'386'350;
constexpr std::uint32_t kClkMdPalHz = 106'406'848;

constexpr std::uint64_t kLineClk = 6840;

constexpr std::uint64_t kHoldClk = 5 * 32768 / 2;

constexpr std::uint8_t kLateFrames = 2;

constexpr std::size_t kGmvHead = 64;
constexpr std::string_view kGmvMagic = "Gens Movie TEST";
constexpr std::size_t kGmvChunk = 256;
constexpr std::size_t kGmvLineMax = 2 * 12 + 4;

struct Line {
    std::array<char, kGmvLineMax> c{};
};

struct GmvPlayers {
    Pad p1 = Pad::Three;
    Pad p2 = Pad::Three;

    [[nodiscard]] Pad layout() const noexcept {
        return p1 == Pad::Six || p2 == Pad::Six ? Pad::Six : Pad::Three;
    }

    [[nodiscard]] bool six() const noexcept { return layout() == Pad::Six; }
};

class GmvText final : public svc::IFile {
public:
    GmvText(
        std::unique_ptr<svc::IFile> file,
        std::uint64_t frames,
        std::string head,
        GmvPlayers players
    )
        : file_(std::move(file)),
          frames_(frames),
          head_(std::move(head)),
          players_(players),
          line_(0) {
        constexpr std::size_t kLogPorts = 2;
        constexpr std::size_t kLineFraming = 4;
        line_ = kLogPorts * width(players_.layout()) + kLineFraming;
    }

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t off, std::span<std::byte> dst) override {
        constexpr std::size_t kGmvFrameBytes = 3;
        std::size_t done = 0;
        if (off < head_.size()) {
            const auto n = static_cast<std::size_t>(
                std::min<std::uint64_t>(dst.size(), head_.size() - off)
            );
            std::copy_n(infra::text_bytes(head_).data() + off, n, dst.data());
            done = n;
        }
        while (done < dst.size()) {
            const std::uint64_t at = off + done - head_.size();
            const std::uint64_t f = at / line_;
            if (f >= frames_)
                break;
            const std::uint64_t count = std::min<std::uint64_t>(
                {frames_ - f, kGmvChunk, (dst.size() - done + (at % line_)) / line_ + 1}
            );
            std::array<std::byte, kGmvFrameBytes * kGmvChunk> raw{};
            const auto got = file_->read_at(
                kGmvHead + kGmvFrameBytes * f,
                std::span(raw).first(static_cast<std::size_t>(kGmvFrameBytes * count))
            );
            if (!got)
                return std::unexpected(got.error());
            const std::uint64_t whole = *got / kGmvFrameBytes;
            if (whole == 0)
                break;
            std::array<char, kGmvChunk * kGmvLineMax> text{};
            for (std::uint64_t i = 0; i < whole; ++i) {
                const Line one = render_(raw.data() + kGmvFrameBytes * i);
                std::copy_n(one.c.data(), line_, text.data() + i * line_);
            }
            const std::size_t skip = static_cast<std::size_t>(at % line_);
            const std::size_t n = std::min(
                dst.size() - done,
                static_cast<std::size_t>(whole * line_) - skip
            );
            std::copy_n(std::as_bytes(std::span(text)).data() + skip, n, dst.data() + done);
            done += n;
            if (whole < count)
                break;
        }
        return done;
    }

    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t, std::span<const std::byte>) override {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EROFS)});
    }

    [[nodiscard]] Ex<svc::FileSize> size() const override {
        return svc::FileSize{head_.size() + frames_ * line_};
    }

    [[nodiscard]] Ex<void> flush() override { return {}; }

private:
    [[nodiscard]] Line render_(const std::byte* b) const {
        constexpr std::size_t kThreeButtonBits = 8;
        constexpr std::size_t kLogPorts = 2;
        constexpr std::size_t kXyzmBits = 4;
        constexpr std::size_t kGmvXyzmAt = 2;
        Line out{};
        std::size_t at = 0;
        out.c[at++] = '|';
        for (unsigned p = 0; p < kLogPorts; ++p) {
            const Pad kind = p == 0 ? players_.p1 : players_.p2;
            const auto pad = static_cast<unsigned>(b[p]);
            const auto xyzm = static_cast<unsigned>(b[kGmvXyzmAt]) >> (kXyzmBits * p);
            for (std::size_t i = 0; i < kThreeButtonBits; ++i)
                out.c[at++] = (pad >> i) & 1u ? '.' : MegaDriveBk2Format::kPadMnemonic[i];
            if (players_.six()) {
                for (std::size_t i = 0; i < kXyzmBits; ++i) {
                    const bool down = kind == Pad::Six && ((xyzm >> i) & 1u) == 0;
                    out.c[at++] = down ? MegaDriveBk2Format::kPadMnemonic[kThreeButtonBits + i]
                                       : '.';
                }
            }
            out.c[at++] = '|';
        }
        out.c[at] = '\n';
        return out;
    }

    std::unique_ptr<svc::IFile> file_;
    std::uint64_t frames_;
    std::string head_;
    GmvPlayers players_;
    std::uint64_t line_;
};

std::string pad_key(unsigned player, const GmvPlayers& players) {
    std::string k = "#";
    for (std::size_t i = 0; i < width(players.layout()); ++i) {
        k += 'P';
        k += static_cast<char>('0' + player);
        k += ' ';
        k += MegaDriveBk2Format::kPadName[i];
        k += '|';
    }
    return k;
}

}

Layout Layout::of(std::uint16_t b) noexcept {
    constexpr unsigned kPadMask = 0x3;
    constexpr unsigned kP2Shift = 2;
    constexpr unsigned kSysShift = 4;
    constexpr unsigned kResetFirstShift = 6;
    constexpr unsigned kGmvShift = 7;
    constexpr unsigned kRegionShift = 8;
    constexpr unsigned kRegionMask = 0x3;
    constexpr unsigned kPalKeyShift = 10;
    constexpr unsigned kPlatformShift = 11;
    constexpr unsigned kCoreShift = 12;

    constexpr std::size_t kRegionCodes = kRegionMask + 1;
    static constexpr std::array<GpgxRegion, kRegionCodes>
        kRegionByCode{GpgxRegion::Auto, GpgxRegion::Usa, GpgxRegion::Europe, GpgxRegion::JapanNtsc};
    Layout l{};
    l.p1 = static_cast<Pad>(b & kPadMask);
    l.p2 = static_cast<Pad>((b >> kP2Shift) & kPadMask);
    l.sys = static_cast<Sys>((b >> kSysShift) & kPadMask);
    l.reset_first = ((b >> kResetFirstShift) & 1u) != 0;
    l.gmv = ((b >> kGmvShift) & 1u) != 0;
    l.region = kRegionByCode[(b >> kRegionShift) & kRegionMask];
    l.pal_key = ((b >> kPalKeyShift) & 1u) != 0;
    l.platform = ((b >> kPlatformShift) & 1u) != 0;
    l.core = ((b >> kCoreShift) & 1u) != 0;
    return l;
}

std::uint16_t Layout::bits() const noexcept {
    constexpr unsigned kP2Shift = 2;
    constexpr unsigned kSysShift = 4;
    constexpr unsigned kResetFirstShift = 6;
    constexpr unsigned kGmvShift = 7;
    constexpr unsigned kRegionShift = 8;
    constexpr unsigned kPalKeyShift = 10;
    constexpr unsigned kPlatformShift = 11;
    constexpr unsigned kCoreShift = 12;
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(p1)
        | static_cast<unsigned>(p2) << kP2Shift
        | static_cast<unsigned>(sys) << kSysShift
        | unsigned{reset_first} << kResetFirstShift
        | unsigned{gmv} << kGmvShift
        | unsigned{std::to_underlying(region)} << kRegionShift
        | unsigned{pal_key} << kPalKeyShift
        | unsigned{platform} << kPlatformShift
        | unsigned{core} << kCoreShift
    );
}

std::optional<CoreRegion> Layout::core_region() const noexcept {
    switch (region) {
        case GpgxRegion::Usa:
            return CoreRegion::Usa;
        case GpgxRegion::Europe:
            return CoreRegion::Europe;
        case GpgxRegion::JapanNtsc:
            return CoreRegion::Japan;
        case GpgxRegion::Auto:
            break;
    }
    return std::nullopt;
}

Ex<std::unique_ptr<svc::IFile>> open_gmv_text(std::unique_ptr<svc::IFile> gmv) {
    constexpr std::size_t kGmvFrameBytes = 3;
    constexpr std::size_t kGmvVersionAt = 0x0F;
    constexpr std::size_t kGmvRerecordsAt = 0x10;
    constexpr std::size_t kGmvRerecordsBytes = 4;
    constexpr std::size_t kGmvPlayer1At = 0x14;
    constexpr std::size_t kGmvPlayer2At = 0x15;
    constexpr std::size_t kGmvFlagsAt = 0x16;
    constexpr unsigned kMultitapFirstPlayer = 1;
    constexpr unsigned kMaxPlayers = 2;
    const auto size = gmv->size();
    if (!size)
        return std::unexpected(size.error());
    if (size->v < kGmvHead)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    std::array<std::byte, kGmvHead> h{};
    const auto got = gmv->read_at(0, h);
    if (!got)
        return std::unexpected(got.error());
    if (*got != h.size())
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    const auto byte = [&](std::size_t i) {
        return static_cast<unsigned>(h[i]);
    };
    if (infra::as_text(std::span(h).first(kGmvMagic.size())) != kGmvMagic)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));

    const GmvPlayers players{
        byte(kGmvPlayer1At) == '6' ? Pad::Six : Pad::Three,
        byte(kGmvPlayer2At) == '6' ? Pad::Six : Pad::Three
    };

    constexpr std::uint64_t kLogPorts = 2;
    constexpr std::uint64_t kLineFraming = 4;
    const std::uint64_t line = kLogPorts * width(players.layout()) + kLineFraming;
    if ((size->v - kGmvHead) / kGmvFrameBytes * line > MovieArchiveFormat::kLogMax)
        return std::unexpected(movie_refusal(CR::TooLong, ERR_SITE()));
    const auto rerecords = infra::load_le<std::uint32_t>(
        std::span(h).subspan<kGmvRerecordsAt, kGmvRerecordsBytes>()
    );
    std::string head = "GmvVersion "
        + std::to_string(byte(kGmvVersionAt))
        + "\nGmvRerecords "
        + std::to_string(rerecords)
        + "\nGmvPlayers "
        + std::to_string(byte(kGmvPlayer1At))
        + " "
        + std::to_string(byte(kGmvPlayer2At))
        + "\nGmvFlags "
        + std::to_string(byte(kGmvFlagsAt))
        + "\nLogKey:"
        + pad_key(kMultitapFirstPlayer, players)
        + pad_key(kMaxPlayers, players)
        + "\n";
    const std::uint64_t frames = (size->v - kGmvHead) / kGmvFrameBytes;
    return std::unique_ptr<svc::IFile>(
        std::make_unique<GmvText>(std::move(gmv), frames, std::move(head), players)
    );
}

infra::OptRef<const MovieArchiveFormat> MegaDriveBk2Format::archive() const noexcept {
    return infra::OptRef<const MovieArchiveFormat>{kBk2Archive};
}

Ex<std::unique_ptr<svc::IFile>> MegaDriveGmvFormat::open_movie(
    const svc::Vfs& vfs,
    std::string_view path
) const {
    auto f = vfs.open(path, svc::OpenMode::Read);

    if (!f && f.error().code == Errc::bad_format)
        return std::unexpected(movie_refusal(MovieRefusal::Archive, ERR_SITE()));
    if (!f)
        return std::unexpected(f.error());
    return open_gmv_text(std::move(*f));
}

bool MegaDriveGmvFormat::rom_matches(const DigestValue& d, const MovieFacts& f) const noexcept {
    (void)d;
    return !f.digest;
}

bool MegaDriveBk2Format::starts_log(std::string_view line) const noexcept {
    return bk2_starts_log(line);
}

bool MegaDriveBk2Format::ends_log(std::string_view line) const noexcept {
    return bk2_ends_log(line);
}

Ex<MovieFacts> MegaDriveBk2Format::header_line(
    std::string_view line,
    const MovieFacts& so_far
) const noexcept {
    constexpr std::size_t kGpgxRegionDigits = 1;
    constexpr std::size_t kFlagDigits = 3;
    constexpr std::uint32_t kFlagsMax = 0xFF;
    constexpr std::uint32_t kGmvFlagSavestate = 0x40;
    constexpr std::uint32_t kGmvFlagMultitap = 0x20;
    constexpr std::uint32_t kGmvFlagPal = 0x80;
    constexpr unsigned kDigitsGmvVersion = 3;
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
            const auto k = read_log_key(ln.value, l);
            if (!k || (l.gmv && k->sys != Sys::None))
                return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
            l = *k;
            svc::PlayerMask ports{};
            if (l.p1 != Pad::None)
                ports = ports.with(svc::kPlayers[0]);
            if (l.p2 != Pad::None)
                ports = ports.with(svc::kPlayers[1]);
            f.ports = ports;
            f.layout = FormatLayout::of(l.bits());
            return f;
        }
        case Bk2Line::Kind::Pair:
            break;
    }
    const std::string_view key = ln.key;
    const std::string_view val = ln.value;
    const auto set_region = [&] {
        const bool pal = l.region == GpgxRegion::Europe
            || (l.region == GpgxRegion::Auto && l.pal_key);
        f.region = pal ? MovieRegion::Pal : MovieRegion::Ntsc;
    };

    const bool gmv_key = key.starts_with("Gmv");
    if (gmv_key != l.gmv && (key != "GmvVersion" || !f.version.unset() || l.bits() != 0))
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    if (key == "MovieVersion") {
        if (!bk2_version_ok(val))
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        f.version = kBizHawkRevision;
    } else if (key == "Platform") {
        if (val != kPlatformName)
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        l.platform = true;
    } else if (key == "Core") {

        if (val != "Genplus-gx")
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        l.core = true;
    } else if (key == "PAL") {
        const auto b = bk2_boolean(val);
        if (!b)
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        l.pal_key = *b;
        set_region();
    } else if (key == "SyncSettings.o.Region" || key == "SyncSettings.Region") {
        const auto r = gpgx_region(val);
        if (!r)
            return std::unexpected(r.error());
        l.region = *r;
        set_region();
    } else if (key == "SyncSettings.o.ForceVDP" || key == "SyncSettings.ForceVDP") {
        const auto v = val.size() <= kGpgxRegionDigits ? infra::parse_int<std::uint32_t>(val)
                                                       : std::nullopt;
        if (!(v == 0u || infra::ieq(val, "Disabled")))
            return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
    } else if (key == "SyncSettings.o.LoadBIOS" || key == "SyncSettings.LoadBIOS") {
        const auto b = bk2_boolean(val);
        if (!b || *b)
            return std::unexpected(movie_refusal(CR::Setting, ERR_SITE()));
    } else if (key == "GmvVersion") {
        const auto v = val.size() <= kDigitsGmvVersion ? infra::parse_int<std::uint32_t>(val)
                                                       : std::nullopt;
        if (!v || *v < '0' || *v > 'Z')
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
        f.version = MovieRevision{static_cast<std::uint16_t>(*v)};
        l.gmv = true;
    } else if (key == "GmvPlayers") {
        const std::size_t sp2 = val.find(' ');
        const std::string_view a_field = val.substr(0, sp2);
        const auto a = a_field.size() <= kFlagDigits ? infra::parse_int<std::uint32_t>(a_field)
                                                     : std::nullopt;
        std::optional<std::uint32_t> b;
        if (sp2 != std::string_view::npos) {
            const std::string_view b_field = val.substr(sp2 + 1);
            if (b_field.size() <= kFlagDigits)
                b = infra::parse_int<std::uint32_t>(b_field);
        }
        const auto ok = [](std::optional<std::uint32_t> c) {
            return c == std::uint32_t{'3'} || c == std::uint32_t{'6'};
        };
        if (!ok(a) || !ok(b))
            return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
    } else if (key == "GmvFlags") {
        const auto v = val.size() <= kFlagDigits ? infra::parse_int<std::uint32_t>(val)
                                                 : std::nullopt;
        if (!v || *v > kFlagsMax || !l.gmv)
            return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));

        if (f.version >= MovieRevision::of_char('A')) {
            if ((*v & kGmvFlagSavestate) != 0)
                return std::unexpected(movie_refusal(CR::Savestate, ERR_SITE()));
            if ((*v & kGmvFlagMultitap) != 0)
                return std::unexpected(movie_refusal(CR::Multitap, ERR_SITE()));
            f.region = (*v & kGmvFlagPal) != 0 ? MovieRegion::Pal : MovieRegion::Ntsc;
        }
    } else {
        const auto d = bk2_prefixed_digest(key, val, f.digest.has_value());
        if (!d)
            return std::unexpected(d.error());
        if (*d)
            f.digest = **d;
    }
    if (const auto n = rerecord_count(key, val))
        f.rerecords = n;
    f.layout = FormatLayout::of(l.bits());
    return f;
}

Ex<void> MegaDriveBk2Format::finish_header(const MovieFacts& f) const noexcept {
    const Layout l = f.layout.view_as<Layout>();
    if (l.p1 == Pad::None)
        return std::unexpected(movie_refusal(CR::PortType, ERR_SITE()));
    if (l.gmv)
        return {};
    if (f.version != kBizHawkRevision || !l.platform || !l.core)
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));

    if (l.region != GpgxRegion::Auto && l.pal_key != (l.region == GpgxRegion::Europe))
        return std::unexpected(movie_refusal(CR::NotAMovie, ERR_SITE()));
    if (!f.digest)
        return std::unexpected(movie_refusal(CR::Checksum, ERR_SITE()));
    return {};
}

Ex<MovieFrame> MegaDriveBk2Format::frame(
    std::string_view line,
    const MovieFacts& f
) const noexcept {
    constexpr std::size_t kSystemGroupBits = 2;
    constexpr std::size_t kGroupsMax = 3;
    const auto text = bk2_frame_text(line);
    if (!text)
        return std::unexpected(text.error());
    line = *text;
    const Layout l = f.layout.view_as<Layout>();

    std::array<LogGroup, kGroupsMax> groups{};
    std::size_t groups_n = 0;
    const auto add = [&](LogGroup g) {
        groups[groups_n++] = g;
    };
    if (l.sys == Sys::First)
        add(std::nullopt);
    add(l.p1);
    if (l.p2 != Pad::None)
        add(l.p2);
    if (l.sys == Sys::Last)
        add(std::nullopt);
    MovieFrame out{};
    std::string_view rest = line.substr(1);
    std::size_t pad = 0;
    for (std::size_t g = 0; g < groups_n; ++g) {
        const LogGroup group = groups[g];
        const std::size_t columns = group ? width(*group) : kSystemGroupBits;
        const std::size_t bar = rest.find('|');
        if (bar != columns)
            return std::unexpected(movie_refusal(CR::BadLine, ERR_SITE()));
        const std::string_view field = rest.substr(0, bar);
        rest.remove_prefix(bar + 1);
        if (!group) {
            const std::string_view names = l.reset_first ? "rP" : "Pr";
            for (std::size_t i = 0; i < kSystemGroupBits; ++i) {
                if (field[i] == '.')
                    continue;
                if (field[i] != names[i])
                    return std::unexpected(movie_refusal(CR::BadLine, ERR_SITE()));

                out.commands = names[i] == 'r' ? out.commands.with_reset()
                                               : out.commands.with_power();
            }
            continue;
        }
        proto::JoyMask m{};
        for (std::size_t i = 0; i < field.size(); ++i) {
            if (field[i] == '.')
                continue;
            if (field[i] != kPadMnemonic[i])
                return std::unexpected(movie_refusal(CR::BadLine, ERR_SITE()));
            m = m.with(kPadBit[i].mask());
        }
        out.mask[pad++] = m;
    }

    if (!rest.empty() && rest != "|")
        return std::unexpected(movie_refusal(CR::BadLine, ERR_SITE()));
    return out;
}

ReplayLead MegaDriveBk2Format::default_lead(const MovieFacts& f) const noexcept {

    return f.layout.view_as<Layout>().gmv ? MegaDriveGmvFormat::kGensLead : kGpgxLead;
}

IReplayCore::SettingNeeds MegaDriveMovieCodec::setting_needs(const MovieFacts& f) const noexcept {
    constexpr std::uint8_t kRegionAuto = 0;
    constexpr std::uint8_t kRegionEu = 2;
    const auto one = [](std::uint8_t v) {
        return static_cast<std::uint16_t>(1u << v);
    };
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
    const Layout l = f.layout.view_as<Layout>();
    const std::uint8_t six = l.p1 == Pad::Six || l.p2 == Pad::Six ? 1u : 0u;
    out.add(
        {.field = kSixButtonField,
         .allowed = proto::ValueSet{one(six)},
         .preferred = proto::FieldValue{six},
         .name = "6 Buttons Mode"}
    );

    if ((!l.gmv && l.region == GpgxRegion::Auto) || (l.gmv && f.region != MovieRegion::Pal)) {

        out.add(
            {.field = kAutoRegionField,
             .allowed = proto::ValueSet{one(kRegionAuto)},
             .preferred = proto::FieldValue{kRegionAuto},
             .name = "Auto Region"}
        );
        out.add(
            {.field = kPriorityField,
             .allowed = proto::ValueSet{one(kUsJpEu)},
             .preferred = proto::FieldValue{kUsJpEu},
             .name = "Priority"}
        );
        return out;
    }
    out.add(
        {.field = kAutoRegionField,
         .allowed = proto::ValueSet{one(kAutoDisabled)},
         .preferred = proto::FieldValue{kAutoDisabled},
         .name = "Auto Region"}
    );
    if (l.gmv) {
        out.add(
            {.field = kRegionField,
             .allowed = proto::ValueSet{one(kRegionEu)},
             .preferred = proto::FieldValue{kRegionEu},
             .name = "Region"}
        );
        return out;
    }

    const std::optional<CoreRegion> core = l.core_region();
    if (!core) {
        out.add(
            {.field = kRegionField,
             .allowed = proto::ValueSet{},
             .preferred = proto::FieldValue{},
             .name = "Region"}
        );
        return out;
    }
    const std::uint8_t want = std::to_underlying(*core);
    out.add(
        {.field = kRegionField,
         .allowed = proto::ValueSet{one(want)},
         .preferred = proto::FieldValue{want},
         .name = "Region"}
    );
    return out;
}

IReplayCore::Raster MegaDriveMovieCodec::raster(const MovieFacts& f) const noexcept {
    constexpr std::uint32_t kPalFramePeriodNs = 20'120'134;
    constexpr std::uint32_t kPalLine0Ns = 3'535'487;
    constexpr std::uint32_t kNtscFramePeriodNs = 16'688'154;
    constexpr std::uint32_t kNtscLine0Ns = 1'783'467;

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

IReplayCore::PowerOn MegaDriveMovieCodec::power_on(const MovieFacts& f) const noexcept {
    constexpr std::uint64_t kHalf = 2;
    constexpr std::uint64_t kPalHalfLines = 626;
    constexpr std::uint64_t kNtscHalfLines = 524;
    constexpr std::uint64_t kPowerOnLineTriple = 3;
    constexpr std::uint64_t kActiveLines = 224;
    const bool pal = f.region == MovieRegion::Pal;
    const std::uint64_t lines2 = pal ? kPalHalfLines : kNtscHalfLines;

    const std::uint64_t phase2 = f.layout.view_as<Layout>().gmv
        ? lines2 / kHalf
        : kPowerOnLineTriple * lines2 / kHalf - kHalf * kActiveLines;
    return {
        .clocks = CoreClocks{kHoldClk + phase2 * kLineClk / kHalf},
        .clock_hz = ClockHz{pal ? kClkMdPalHz : kClkMdNtscHz},
        .late_frames = LateFrames{kLateFrames}
    };
}

std::optional<IMovieFormat::DigestSpan> MegaDriveBk2Format::rom_digest_span(
    std::span<const std::uint8_t> head,
    proto::FileSize size
) const noexcept {
    constexpr proto::FileSize kMinRomBytes{0x200};
    constexpr std::size_t kSmdMarkerLo = 8;
    constexpr std::size_t kSmdMarkerHi = 9;
    constexpr std::uint8_t kSmdMarkerLoByte = 0xAA;
    constexpr std::uint8_t kSmdMarkerHiByte = 0xBB;

    if (size < kMinRomBytes || !copier_header(size).empty())
        return std::nullopt;
    if (head.size() > kSmdMarkerHi
        && head[kSmdMarkerLo] == kSmdMarkerLoByte
        && head[kSmdMarkerHi] == kSmdMarkerHiByte)
        return std::nullopt;
    return span_past(size, proto::FileSize{});
}

}
