// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/movie_codec.h"
#include "cores/movie_system.h"

#include "cores/megadrive_movie_codec.h"
#include "cores/movie_archive.h"
#include "cores/nes_movie_codec.h"
#include "cores/snes_bk2_codec.h"
#include "cores/psx_movie_codec.h"
#include "cores/snes_lsmv_codec.h"
#include "svc/file.h"
#include "svc/vfs.h"

#include <string>
#include <vector>

namespace mister::cores {
namespace {

constexpr NesMovieCodec kNesMovieCodec{};
constexpr SnesLsmvCodec kSnesLsmvCodec{};
constexpr SnesBk2Codec kSnesBk2Codec{};
constexpr MegaDriveMovieCodec kMegaDriveMovieCodec{};
constexpr PsxMovieCodec kPsxMovieCodec{};

struct CodecRow {
    CoreKind kind;
    std::string_view name;
    std::string_view ext;
    const IMovieCodec* codec;
};
constexpr std::array<CodecRow, 6> kMovieCodecs{{
    {CoreKind::Generic, "NES", "fm2", &kNesMovieCodec},
    {CoreKind::Snes, "SNES", "lsmv", &kSnesLsmvCodec},
    {CoreKind::Snes, "SNES", "bk2", &kSnesBk2Codec},
    {CoreKind::MegaDrive, "MegaDrive", "bk2", &kMegaDriveMovieCodec},
    {CoreKind::MegaDrive, "MegaDrive", "gmv", &kMegaDriveMovieCodec},
    {CoreKind::Psx, "PSX", "bk2", &kPsxMovieCodec},
}};

std::string_view extension_of(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos) return {};
    const std::size_t slash = path.rfind('/');
    if (slash != std::string_view::npos && slash > dot) return {};
    return path.substr(dot + 1);
}

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
        };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

}  // namespace

unsigned IMovieCodec::SettingNeed::value_in(const proto::StatusWord& live) const noexcept {
    return status_field(live, lo, width);
}

bool IMovieCodec::SettingNeed::met_by(const proto::StatusWord& live) const noexcept {
    const unsigned v = value_in(live);
    return v < 16u && ((allowed >> v) & 1u) != 0;
}

IMovieCodec::SettingVerdict IMovieCodec::check_settings(const proto::StatusWord& live,
                                                        const Facts& f) const noexcept {
    const SettingNeeds needs = setting_needs(f);
    for (const SettingNeed& n : needs.view()) {
        if (!n.met_by(live)) return {false, n.name};
    }
    return {};
}

unsigned IMovieCodec::status_field(const proto::StatusWord& s, unsigned lo,
                                   unsigned width) noexcept {
    unsigned v = 0;
    for (unsigned i = 0; i < width && lo + i < proto::StatusWord::kBits; ++i) {
        if (s.get_bit(proto::StatusBit{static_cast<std::uint8_t>(lo + i)})) v |= 1u << i;
    }
    return v;
}

Ex<std::unique_ptr<svc::IFile>> IMovieCodec::open_plain_or_archive(
    const MovieArchiveFormat* container, const svc::Vfs& vfs, std::string_view path) {
    if (container != nullptr) return open_movie_archive(vfs, path, *container);
    return vfs.open(path, svc::OpenMode::Read);
}

Ex<IMovieCodec::DigestSource> IMovieCodec::head_digest_source(const svc::Vfs& vfs,
                                                              std::string_view rom) const {
    const auto refuse = [](Refusal r) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(r)});
    };
    auto f = vfs.open(rom, svc::OpenMode::Read);
    if (!f) return std::unexpected(f.error());
    const auto size = (*f)->size();
    if (!size) return std::unexpected(size.error());
    if (size->v > kRomMax) return refuse(Refusal::TooLong);
    std::array<std::uint8_t, kRomHead> head{};
    const auto got = (*f)->read_at(0, std::as_writable_bytes(std::span<std::uint8_t>(head)));
    if (!got) return std::unexpected(got.error());
    const auto span = rom_digest_span(std::span<const std::uint8_t>(head.data(), *got), size->v);
    if (!span) return refuse(Refusal::System);
    return DigestSource{.file = std::string(rom), .span = *span, .prefix = {}};
}

std::optional<std::uint32_t> IMovieCodec::power_on_ns(const proto::StatusWord& live,
                                                      const Facts& f) const noexcept {
    const PowerOn p = power_on(f);
    if (p.clocks == 0) return 0;
    if (p.clock_hz == 0 || p.width > 4) return std::nullopt;
    if (p.width != 0 && ((p.when >> status_field(live, p.lo, p.width)) & 1u) == 0u) return 0;
    if (p.clocks / p.clock_hz >= 5) return std::nullopt;

    const std::uint64_t ns = p.clocks / p.clock_hz * 1'000'000'000u +
                             p.clocks % p.clock_hz * 1'000'000'000u / p.clock_hz;
    if (ns > 0xFFFF'FFFFu) return std::nullopt;
    return static_cast<std::uint32_t>(ns);
}

const IMovieCodec* movie_codec_for(std::string_view conf_str_name,
                                   std::string_view movie_path) noexcept {
    const std::string_view ext = extension_of(movie_path);
    for (const CodecRow& r : kMovieCodecs) {
        if (r.name == conf_str_name && iequal(r.ext, ext)) return r.codec;
    }
    return nullptr;
}

const IMovieCodec* movie_codec_for_path(std::string_view movie_path) noexcept {
    const std::string_view ext = extension_of(movie_path);
    const IMovieCodec* found = nullptr;
    unsigned n = 0;
    for (const CodecRow& r : kMovieCodecs) {
        if (!iequal(r.ext, ext)) continue;
        found = r.codec;
        ++n;
    }
    return n == 1 ? found : nullptr;
}

std::optional<MovieSystem> movie_system_for_path(std::string_view movie_path) noexcept {
    const std::string_view ext = extension_of(movie_path);
    const CodecRow* found = nullptr;
    unsigned n = 0;
    for (const CodecRow& r : kMovieCodecs) {
        if (!iequal(r.ext, ext)) continue;
        found = &r;
        ++n;
    }
    if (n != 1 || found == nullptr) return std::nullopt;
    return MovieSystem{.kind = found->kind, .conf_str_name = found->name, .codec = found->codec};
}

bool has_platform(std::string_view text, std::string_view name) noexcept {
    std::string line = "Platform ";
    line.append(name.begin(), name.end());
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find('\n', at);
        std::string_view row =
            text.substr(at, (nl == std::string_view::npos ? text.size() : nl) - at);
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (row == line) return true;
        if (nl == std::string_view::npos) break;
        at = nl + 1;
    }
    return false;
}

std::optional<MovieSystem> movie_system_for(const svc::Vfs& vfs,
                                            std::string_view movie_path) noexcept {
    if (auto u = movie_system_for_path(movie_path)) return u;
    if (!iequal(extension_of(movie_path), "bk2")) return std::nullopt;
    auto f = open_movie_archive(vfs, movie_path, kBk2Archive);
    if (!f) return std::nullopt;
    std::array<std::byte, 8192> raw{};
    const auto n = (*f)->read_at(0, raw);
    if (!n || *n == 0) return std::nullopt;
    const std::string_view text{reinterpret_cast<const char*>(raw.data()), *n};
    if (has_platform(text, "SNES")) {
        const IMovieCodec* c = movie_codec_for("SNES", movie_path);
        if (c == nullptr) return std::nullopt;
        return MovieSystem{CoreKind::Snes, "SNES", c};
    }
    if (has_platform(text, "GEN")) {
        const IMovieCodec* c = movie_codec_for("MegaDrive", movie_path);
        if (c == nullptr) return std::nullopt;
        return MovieSystem{CoreKind::MegaDrive, "MegaDrive", c};
    }
    if (has_platform(text, "PSX")) {
        const IMovieCodec* c = movie_codec_for("PSX", movie_path);
        if (c == nullptr) return std::nullopt;
        return MovieSystem{CoreKind::Psx, "PSX", c};
    }
    return std::nullopt;
}

[[nodiscard]] Ex<IMovieCodec::Facts> read_movie_facts(const svc::Vfs& vfs, const IMovieCodec& codec,
                                                      std::string_view movie_path) {
    auto f = codec.open_movie(vfs, movie_path);
    if (!f) return std::unexpected(f.error());
    IMovieCodec::Facts facts{};
    std::vector<char> buf(4096);
    std::string line;
    std::uint64_t off = 0;
    for (;;) {
        const auto got =
            (*f)->read_at(off, std::as_writable_bytes(std::span(buf.data(), buf.size())));
        if (!got) return std::unexpected(got.error());
        if (*got == 0) break;
        off += *got;
        for (std::size_t i = 0; i < *got; ++i) {
            const char ch = buf[i];
            if (ch == '\n') {
                std::string_view row{line};
                if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
                if (codec.starts_log(row)) {
                    if (auto d = codec.finish_header(facts); !d) return std::unexpected(d.error());
                    return facts;
                }
                auto next = codec.header_line(row, facts);
                if (!next) return std::unexpected(next.error());
                facts = *next;
                line.clear();
            } else if (line.size() < IMovieCodec::kLineMax) {
                line.push_back(ch);
            }
        }
        if (*got < buf.size()) break;
    }
    if (auto d = codec.finish_header(facts); !d) return std::unexpected(d.error());
    return facts;
}

bool movie_system_supported(const MovieSystem& sys) noexcept {
    switch (sys.kind) {
        case CoreKind::Generic:
        case CoreKind::Snes:
        case CoreKind::MegaDrive:
        case CoreKind::Psx:
        case CoreKind::Menu:
            return sys.codec != nullptr || sys.kind == CoreKind::Menu;
        default:
            return false;
    }
}

bool movie_system_plays(std::string_view conf_str_name) noexcept {
    for (const CodecRow& r : kMovieCodecs) {
        if (r.name != conf_str_name) continue;
        if (movie_system_supported(MovieSystem{r.kind, r.name, r.codec})) return true;
    }
    return false;
}

}  // namespace mister::cores
