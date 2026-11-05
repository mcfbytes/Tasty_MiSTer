// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megadrive_movie_codec.h"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <charconv>
#include <string>
#include <system_error>

#include "cores/movie_archive.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores {
namespace {

using CR = IMovieCodec::Refusal;
using Pad = MegaDriveMovieCodec::Pad;
using Sys = MegaDriveMovieCodec::Sys;
using Layout = MegaDriveMovieCodec::Layout;

Error refuse(CR r, infra::ErrorSite site) {
    return Error{Errc::bad_format, site, static_cast<std::uint32_t>(r)};
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
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

bool iequal(std::string_view a, std::string_view b) {
    constexpr char kCaseBit = 'a' - 'A';
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               const auto lo = [](char c) {
                   return c >= 'A' && c <= 'Z' ? static_cast<char>(c + kCaseBit) : c;
               };
               return lo(x) == lo(y);
           });
}

std::optional<bool> boolean(std::string_view v) {
    if (v == "1" || iequal(v, "true"))
        return true;
    if (v == "0" || iequal(v, "false"))
        return false;
    return std::nullopt;
}

std::optional<DigestValue> read_digest(std::string_view v) {
    constexpr std::size_t kHexPerByte = 2;
    if (const auto colon = v.find(':'); colon != std::string_view::npos)
        v.remove_prefix(colon + 1);
    for (const DigestKind k : {DigestKind::Sha1, DigestKind::Md5, DigestKind::Crc32}) {
        if (v.size() == kHexPerByte * digest_len(k))
            return parse_hex_digest(v, k);
    }
    return std::nullopt;
}

std::optional<std::uint8_t> gpgx_region(std::string_view v) {
    constexpr unsigned kRegionJapanPal = 4;
    constexpr std::size_t kGpgxRegionDigits = 1;
    static constexpr std::array<std::string_view, 5>
        kNames{"Autodetect", "USA", "Europe", "Japan_NTSC", "Japan_PAL"};
    std::optional<std::uint32_t> n = decimal(v, kGpgxRegionDigits);
    for (std::size_t i = 0; !n && i < kNames.size(); ++i)
        if (iequal(v, kNames[i]))
            n = static_cast<std::uint32_t>(i);
    if (!n || *n > kRegionJapanPal)
        return std::nullopt;

    return static_cast<std::uint8_t>(*n);
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
        if (name.size() < kPrefixBytes
            || name.substr(0, kPrefixBytes) != std::string_view(prefix, kPrefixBytes))
            return std::nullopt;
        if (name.substr(kPrefixBytes) != MegaDriveMovieCodec::kPadName[i])
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

struct SettingRule {
    std::uint8_t lo;
    std::uint8_t width;
    std::uint8_t allowed;
    std::string_view name;
};

constexpr std::array<SettingRule, 8> kRules{{
    {4, 1, 0x01, "Swap Joysticks"},
    {37, 3, 0x01, "Multitap"},
    {18, 2, 0x01, "Mouse"},
    {21, 2, 0x01, "Keyboard"},
    {40, 2, 0x01, "Gun Control"},
    {62, 2, 0x01, "SNAC"},
    {61, 1, 0x01, "Pause When OSD is Open"},
    {12, 1, 0x01, "TMSS"},
}};
constexpr std::uint8_t kSixButtonLo = 5;
constexpr std::uint8_t kRegionLo = 6;
constexpr std::uint8_t kAutoRegionLo = 8;
constexpr std::uint8_t kPriorityLo = 27;
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

class GmvText final : public svc::IFile {
public:
    GmvText(
        std::unique_ptr<svc::IFile> file,
        std::uint64_t frames,
        std::string head,
        bool six,
        bool xyzm1,
        bool xyzm2
    )
        : file_(std::move(file)),
          frames_(frames),
          head_(std::move(head)),
          six_(six),
          xyzm_{xyzm1, xyzm2},
          line_(0) {
        constexpr std::size_t kLogPorts = 2;
        constexpr std::size_t kLineFraming = 4;
        line_ = kLogPorts * width(six ? Pad::Six : Pad::Three) + kLineFraming;
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

    svc::FileKind kind() const noexcept override { return file_->kind(); }

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
            const auto pad = static_cast<unsigned>(b[p]);
            const auto xyzm = static_cast<unsigned>(b[kGmvXyzmAt]) >> (kXyzmBits * p);
            for (std::size_t i = 0; i < kThreeButtonBits; ++i)
                out.c[at++] = (pad >> i) & 1u ? '.' : MegaDriveMovieCodec::kPadMnemonic[i];
            if (six_) {
                for (std::size_t i = 0; i < kXyzmBits; ++i) {
                    const bool down = xyzm_[p] && ((xyzm >> i) & 1u) == 0;
                    out.c[at++] = down ? MegaDriveMovieCodec::kPadMnemonic[kThreeButtonBits + i]
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
    bool six_;
    std::array<bool, 2> xyzm_;
    std::uint64_t line_;
};

std::string pad_key(unsigned player, bool six) {
    std::string k = "#";
    for (std::size_t i = 0; i < width(six ? Pad::Six : Pad::Three); ++i) {
        k += 'P';
        k += static_cast<char>('0' + player);
        k += ' ';
        k += MegaDriveMovieCodec::kPadName[i];
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
    constexpr unsigned kRegionMask = 0x7;
    constexpr unsigned kPalKeyShift = 11;
    constexpr unsigned kPlatformShift = 12;
    constexpr unsigned kCoreShift = 13;
    Layout l{};
    l.p1 = static_cast<Pad>(b & kPadMask);
    l.p2 = static_cast<Pad>((b >> kP2Shift) & kPadMask);
    l.sys = static_cast<Sys>((b >> kSysShift) & kPadMask);
    l.reset_first = ((b >> kResetFirstShift) & 1u) != 0;
    l.gmv = ((b >> kGmvShift) & 1u) != 0;
    l.region = static_cast<std::uint8_t>((b >> kRegionShift) & kRegionMask);
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
    constexpr unsigned kRegionMask = 0x7;
    constexpr unsigned kPalKeyShift = 11;
    constexpr unsigned kPlatformShift = 12;
    constexpr unsigned kCoreShift = 13;
    return static_cast<std::uint16_t>(
        static_cast<unsigned>(p1)
        | static_cast<unsigned>(p2) << kP2Shift
        | static_cast<unsigned>(sys) << kSysShift
        | unsigned{reset_first} << kResetFirstShift
        | unsigned{gmv} << kGmvShift
        | unsigned{region & kRegionMask} << kRegionShift
        | unsigned{pal_key} << kPalKeyShift
        | unsigned{platform} << kPlatformShift
        | unsigned{core} << kCoreShift
    );
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
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
    std::array<std::byte, kGmvHead> h{};
    const auto got = gmv->read_at(0, h);
    if (!got)
        return std::unexpected(got.error());
    if (*got != h.size())
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
    const auto byte = [&](std::size_t i) {
        return static_cast<unsigned>(h[i]);
    };
    if (infra::as_text(std::span(h).first(kGmvMagic.size())) != kGmvMagic)
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));

    const bool six1 = byte(kGmvPlayer1At) == '6', six2 = byte(kGmvPlayer2At) == '6';
    const bool six = six1 || six2;

    constexpr std::uint64_t kLogPorts = 2;
    constexpr std::uint64_t kLineFraming = 4;
    const std::uint64_t line = kLogPorts * width(six ? Pad::Six : Pad::Three) + kLineFraming;
    if ((size->v - kGmvHead) / kGmvFrameBytes * line > MovieArchiveFormat::kLogMax)
        return std::unexpected(refuse(CR::TooLong, ERR_SITE()));
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
        + pad_key(kMultitapFirstPlayer, six)
        + pad_key(kMaxPlayers, six)
        + "\n";
    const std::uint64_t frames = (size->v - kGmvHead) / kGmvFrameBytes;
    return std::unique_ptr<svc::IFile>(
        std::make_unique<GmvText>(std::move(gmv), frames, std::move(head), six, six1, six2)
    );
}

const MovieArchiveFormat* MegaDriveMovieCodec::archive() const noexcept {
    return &kBk2Archive;
}

Ex<std::unique_ptr<svc::IFile>> MegaDriveMovieCodec::open_movie(
    const svc::Vfs& vfs,
    std::string_view path
) const {
    auto f = vfs.open(path, svc::OpenMode::Read);

    if (!f && f.error().code == Errc::bad_format)
        return std::unexpected(refuse(CR::Archive, ERR_SITE()));
    if (!f)
        return std::unexpected(f.error());
    std::array<std::byte, kGmvMagic.size()> m{};
    const auto got = (*f)->read_at(0, m);
    if (!got)
        return std::unexpected(got.error());
    if (*got == m.size() && infra::as_text(std::span(m)) == kGmvMagic) {
        return open_gmv_text(std::move(*f));
    }
    f->reset();
    return open_movie_archive(vfs, path, kBk2Archive);
}

bool MegaDriveMovieCodec::starts_log(std::string_view line) const noexcept {
    return !line.empty() && line.front() == '|';
}

bool MegaDriveMovieCodec::ends_log(std::string_view line) const noexcept {
    return trim(line) == "[/Input]";
}

Ex<IMovieCodec::Facts> MegaDriveMovieCodec::header_line(
    std::string_view line,
    const Facts& so_far
) const noexcept {
    constexpr std::string_view kLogKeyPrefix = "LogKey:";
    constexpr unsigned kPort2Bit = 2;
    constexpr unsigned kRegionAuto = 0;
    constexpr unsigned kRegionEu = 2;
    constexpr unsigned kRegionJapanPal = 4;
    constexpr std::size_t kGpgxRegionDigits = 1;
    constexpr std::size_t kFlagDigits = 3;
    constexpr std::uint32_t kFlagsMax = 0xFF;
    constexpr std::uint32_t kGmvFlagSavestate = 0x40;
    constexpr std::uint32_t kGmvFlagMultitap = 0x20;
    constexpr std::uint32_t kGmvFlagPal = 0x80;
    constexpr unsigned kDigitsGmvVersion = 3;
    constexpr std::uint8_t kBizHawkVersion = 2;
    Facts f = so_far;
    Layout l = Layout::of(f.layout);
    line = trim(line);
    if (line.empty())
        return f;

    if (line == "@Core"
        || line == "@CoreText"
        || line == "@SaveRam"
        || line.starts_with("SavestateBinaryBase64Blob")) {
        return std::unexpected(refuse(CR::Savestate, ERR_SITE()));
    }
    if (line.starts_with(kLogKeyPrefix)) {
        const auto k = read_log_key(line.substr(kLogKeyPrefix.size()), l);
        if (!k || (l.gmv && k->sys != Sys::None))
            return std::unexpected(refuse(CR::PortType, ERR_SITE()));
        l = *k;
        f.ports = static_cast<std::uint8_t>(
            (l.p1 != Pad::None ? 1u : 0u) | (l.p2 != Pad::None ? kPort2Bit : 0u)
        );
        f.layout = l.bits();
        return f;
    }
    if (line.size() > kLineMax)
        return f;
    const std::size_t sp = line.find(' ');
    const std::string_view key = line.substr(0, sp);
    const std::string_view val = sp == std::string_view::npos ? std::string_view{}
                                                              : trim(line.substr(sp + 1));
    const auto set_region = [&] {
        const bool pal = l.region == kRegionEu || (l.region == kRegionAuto && l.pal_key);
        f.region = pal ? Region::Pal : Region::Ntsc;
    };

    const bool gmv_key = key.starts_with("Gmv");
    if (gmv_key != l.gmv && (key != "GmvVersion" || f.version != 0 || l.bits() != 0))
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
    if (key == "MovieVersion") {
        if (!val.starts_with("BizHawk v2"))
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        f.version = kBizHawkVersion;
    } else if (key == "Platform") {
        if (val != "GEN")
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        l.platform = true;
    } else if (key == "Core") {

        if (val != "Genplus-gx")
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        l.core = true;
    } else if (key == "StartsFromSavestate" || key == "StartsFromSaveRam") {
        const auto b = boolean(val);
        if (!b)
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        if (*b)
            return std::unexpected(refuse(CR::Savestate, ERR_SITE()));
    } else if (key == "SHA1" || (key == "MD5" && !f.has_digest)) {
        const auto d = read_digest(val);
        if (!d)
            return std::unexpected(refuse(CR::Checksum, ERR_SITE()));
        f.digest = *d;
        f.has_digest = true;
    } else if (key == "PAL") {
        const auto b = boolean(val);
        if (!b)
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        l.pal_key = *b;
        set_region();
    } else if (key == "SyncSettings.o.Region" || key == "SyncSettings.Region") {
        const auto r = gpgx_region(val);
        if (!r)
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        if (*r == kRegionJapanPal)
            return std::unexpected(refuse(CR::Setting, ERR_SITE()));

        l.region = *r;
        set_region();
    } else if (key == "SyncSettings.o.ForceVDP" || key == "SyncSettings.ForceVDP") {
        const auto v = decimal(val, kGpgxRegionDigits);
        if (!(v == 0u || iequal(val, "Disabled")))
            return std::unexpected(refuse(CR::Setting, ERR_SITE()));
    } else if (key == "SyncSettings.o.LoadBIOS" || key == "SyncSettings.LoadBIOS") {
        const auto b = boolean(val);
        if (!b || *b)
            return std::unexpected(refuse(CR::Setting, ERR_SITE()));
    } else if (key == "GmvVersion") {
        const auto v = decimal(val, kDigitsGmvVersion);
        if (!v || *v < '0' || *v > 'Z')
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
        f.version = static_cast<std::uint16_t>(*v);
        l.gmv = true;
    } else if (key == "GmvPlayers") {
        const std::size_t sp2 = val.find(' ');
        const auto a = decimal(val.substr(0, sp2), kFlagDigits);
        const auto b = sp2 == std::string_view::npos ? std::nullopt
                                                     : decimal(val.substr(sp2 + 1), kFlagDigits);
        const auto ok = [](std::optional<std::uint32_t> c) {
            return c == std::uint32_t{'3'} || c == std::uint32_t{'6'};
        };
        if (!ok(a) || !ok(b))
            return std::unexpected(refuse(CR::PortType, ERR_SITE()));
    } else if (key == "GmvFlags") {
        const auto v = decimal(val, kFlagDigits);
        if (!v || *v > kFlagsMax || !l.gmv)
            return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));

        if (f.version >= 'A') {
            if ((*v & kGmvFlagSavestate) != 0)
                return std::unexpected(refuse(CR::Savestate, ERR_SITE()));
            if ((*v & kGmvFlagMultitap) != 0)
                return std::unexpected(refuse(CR::Multitap, ERR_SITE()));
            f.region = (*v & kGmvFlagPal) != 0 ? Region::Pal : Region::Ntsc;
        }
    }
    note_rerecords(key, val, f);
    f.layout = l.bits();
    return f;
}

Ex<void> MegaDriveMovieCodec::finish_header(const Facts& f) const noexcept {
    constexpr unsigned kRegionAuto = 0;
    constexpr unsigned kRegionEu = 2;
    constexpr std::uint8_t kBizHawkVersion = 2;
    const Layout l = Layout::of(f.layout);
    if (l.p1 == Pad::None)
        return std::unexpected(refuse(CR::PortType, ERR_SITE()));
    if (l.gmv)
        return {};
    if (f.version != kBizHawkVersion || !l.platform || !l.core)
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));

    if (l.region != kRegionAuto && l.pal_key != (l.region == kRegionEu))
        return std::unexpected(refuse(CR::NotAMovie, ERR_SITE()));
    if (!f.has_digest)
        return std::unexpected(refuse(CR::Checksum, ERR_SITE()));
    return {};
}

Ex<IMovieCodec::Frame> MegaDriveMovieCodec::frame(
    std::string_view line,
    const Facts& f
) const noexcept {
    constexpr std::size_t kSystemGroupBits = 2;
    constexpr std::uint32_t kLogCommandSoft = 0x1;
    constexpr std::uint32_t kLogCommandHard = 0x2;
    while (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    if (line.size() > kLineMax || !starts_log(line))
        return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
    const Layout l = Layout::of(f.layout);
    std::array<std::size_t, 3> widths{};
    std::array<int, 3> role{};
    std::size_t groups = 0;
    const auto add = [&](std::size_t w, int r) {
        widths[groups] = w;
        role[groups++] = r;
    };
    if (l.sys == Sys::First)
        add(kSystemGroupBits, -1);
    add(width(l.p1), 0);
    if (l.p2 != Pad::None)
        add(width(l.p2), 1);
    if (l.sys == Sys::Last)
        add(kSystemGroupBits, -1);
    Frame out{};
    std::string_view rest = line.substr(1);
    for (std::size_t g = 0; g < groups; ++g) {
        const std::size_t bar = rest.find('|');
        if (bar != widths[g])
            return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
        const std::string_view field = rest.substr(0, bar);
        rest.remove_prefix(bar + 1);
        if (role[g] < 0) {
            const std::string_view names = l.reset_first ? "rP" : "Pr";
            for (std::size_t i = 0; i < kSystemGroupBits; ++i) {
                if (field[i] == '.')
                    continue;
                if (field[i] != names[i])
                    return std::unexpected(refuse(CR::BadLine, ERR_SITE()));

                out.commands = static_cast<std::uint8_t>(
                    out.commands | (names[i] == 'r' ? kLogCommandSoft : kLogCommandHard)
                );
            }
            continue;
        }
        std::uint32_t m = 0;
        for (std::size_t i = 0; i < field.size(); ++i) {
            if (field[i] == '.')
                continue;
            if (field[i] != kPadMnemonic[i])
                return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
            m |= 1u << kPadBit[i];
        }
        out.mask[static_cast<std::size_t>(role[g])] = m;
    }

    if (!rest.empty() && rest != "|")
        return std::unexpected(refuse(CR::BadLine, ERR_SITE()));
    return out;
}

IMovieCodec::SettingNeeds MegaDriveMovieCodec::setting_needs(const Facts& f) const noexcept {
    constexpr unsigned kRegionAuto = 0;
    constexpr unsigned kRegionEu = 2;
    constexpr std::uint8_t kOptionWidth = 2;
    constexpr std::uint8_t kSixButtonWidth = 1;
    constexpr std::size_t kCoreRegionCodes = 4;
    const auto one = [](std::uint8_t v) {
        return static_cast<std::uint16_t>(1u << v);
    };
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
    const Layout l = Layout::of(f.layout);
    const std::uint8_t six = l.p1 == Pad::Six || l.p2 == Pad::Six ? 1u : 0u;
    out.add(
        {.lo = kSixButtonLo,
         .width = kSixButtonWidth,
         .allowed = one(six),
         .preferred = six,
         .name = "6 Buttons Mode"}
    );

    if ((!l.gmv && l.region == kRegionAuto) || (l.gmv && f.region != Region::Pal)) {

        out.add(
            {.lo = kAutoRegionLo,
             .width = kOptionWidth,
             .allowed = one(kRegionAuto),
             .preferred = kRegionAuto,
             .name = "Auto Region"}
        );
        out.add(
            {.lo = kPriorityLo,
             .width = kOptionWidth,
             .allowed = one(kUsJpEu),
             .preferred = kUsJpEu,
             .name = "Priority"}
        );
        return out;
    }
    out.add(
        {.lo = kAutoRegionLo,
         .width = kOptionWidth,
         .allowed = one(kAutoDisabled),
         .preferred = kAutoDisabled,
         .name = "Auto Region"}
    );
    if (l.gmv) {
        out.add(
            {.lo = kRegionLo,
             .width = kOptionWidth,
             .allowed = one(kRegionEu),
             .preferred = kRegionEu,
             .name = "Region"}
        );
        return out;
    }
    static constexpr std::array<std::uint8_t, kCoreRegionCodes> kCoreRegion{0, 1, 2, 0};
    const bool known = l.region < kCoreRegion.size();
    const std::uint8_t want = known ? kCoreRegion[l.region] : 0;
    out.add(
        {.lo = kRegionLo,
         .width = kOptionWidth,
         .allowed = known ? one(want) : std::uint16_t{0},
         .preferred = want,
         .name = "Region"}
    );
    return out;
}

IMovieCodec::Raster MegaDriveMovieCodec::raster(const Facts& f) const noexcept {
    constexpr std::uint32_t kPalFramePeriodNs = 20'120'134;
    constexpr std::uint32_t kPalLine0Ns = 3'535'487;
    constexpr std::uint32_t kNtscFramePeriodNs = 16'688'154;
    constexpr std::uint32_t kNtscLine0Ns = 1'783'467;

    if (f.region == Region::Pal)
        return {.period_ns = kPalFramePeriodNs, .line0_ns = kPalLine0Ns};
    return {.period_ns = kNtscFramePeriodNs, .line0_ns = kNtscLine0Ns};
}

IMovieCodec::PowerOn MegaDriveMovieCodec::power_on(const Facts& f) const noexcept {
    constexpr std::uint64_t kHalf = 2;
    constexpr std::uint64_t kPalHalfLines = 626;
    constexpr std::uint64_t kNtscHalfLines = 524;
    constexpr std::uint64_t kPowerOnLineTriple = 3;
    constexpr std::uint64_t kActiveLines = 224;
    const bool pal = f.region == Region::Pal;
    const std::uint64_t lines2 = pal ? kPalHalfLines : kNtscHalfLines;

    const std::uint64_t phase2 = Layout::of(f.layout).gmv
        ? lines2 / kHalf
        : kPowerOnLineTriple * lines2 / kHalf - kHalf * kActiveLines;
    return {
        .clocks = kHoldClk + phase2 * kLineClk / kHalf,
        .clock_hz = pal ? kClkMdPalHz : kClkMdNtscHz,
        .lo = 0,
        .width = 0,
        .when = 0,
        .late_frames = kLateFrames
    };
}

std::int32_t MegaDriveMovieCodec::default_lead(const Facts& f) const noexcept {
    return Layout::of(f.layout).gmv ? kGensLead : kGpgxLead;
}

std::optional<IMovieCodec::DigestSpan> MegaDriveMovieCodec::rom_digest_span(
    std::span<const std::uint8_t> head,
    proto::FileSize size
) const noexcept {
    constexpr std::uint64_t kMinRomBytes = 0x200;
    constexpr std::uint64_t kKiB = 1024;
    constexpr std::uint64_t kCopierHeaderBytes = 512;
    constexpr std::uint64_t kSmdHeaderBytes = 128;
    constexpr std::size_t kSmdMarkerLo = 8;
    constexpr std::size_t kSmdMarkerHi = 9;
    constexpr std::uint8_t kSmdMarkerLoByte = 0xAA;
    constexpr std::uint8_t kSmdMarkerHiByte = 0xBB;
    const std::uint64_t bytes = size.v;

    if (bytes < kMinRomBytes
        || bytes % kKiB == kCopierHeaderBytes
        || bytes % kKiB == kSmdHeaderBytes)
        return std::nullopt;
    if (head.size() > kSmdMarkerHi
        && head[kSmdMarkerLo] == kSmdMarkerLoByte
        && head[kSmdMarkerHi] == kSmdMarkerHiByte)
        return std::nullopt;
    return DigestSpan{.offset = 0, .length = bytes};
}

bool MegaDriveMovieCodec::rom_matches(const DigestValue& d, const Facts& f) const noexcept {
    if (Layout::of(f.layout).gmv && !f.has_digest)
        return true;
    return IMovieCodec::rom_matches(d, f);
}

}
