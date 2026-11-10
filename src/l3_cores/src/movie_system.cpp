// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/movie_system.h"

#include "cores/movie_archive.h"
#include "infra/ascii.h"
#include "movie_codec_detail.h"
#include "svc/file.h"
#include "svc/vfs.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mister::cores {
namespace {

std::string playable_platforms() {
    std::vector<std::string_view> platforms;
    for (const MovieCodecRow& r : movie_codec_rows()) {
        if (r.bk2_platform.empty())
            continue;
        if (std::find(platforms.begin(), platforms.end(), r.display_name) != platforms.end())
            continue;
        platforms.push_back(r.display_name);
    }
    std::string out;
    for (std::size_t i = 0; i < platforms.size(); ++i) {
        if (i != 0)
            out += i + 1 == platforms.size() ? " and " : ", ";
        out += platforms[i];
    }
    return out;
}

std::optional<MovieSystem> system_for_platform(
    std::string_view platform,
    std::string_view movie_path
) noexcept {
    const std::string_view ext = movie_path_extension(movie_path);
    for (const MovieCodecRow& r : movie_codec_rows()) {
        if (r.bk2_platform != platform || !infra::ieq(r.ext, ext))
            continue;
        return MovieSystem{
            .kind = r.kind,
            .conf_str_name = r.name,
            .codec = infra::OptRef<const MovieCodec>{r.codec}
        };
    }
    return std::nullopt;
}

}

std::optional<MovieSystem> movie_system_for_path(std::string_view movie_path) noexcept {
    const MovieCodecRow* row = unique_row_for_ext(movie_path);
    if (row == nullptr)
        return std::nullopt;
    return MovieSystem{
        .kind = row->kind,
        .conf_str_name = row->name,
        .codec = infra::OptRef<const MovieCodec>{row->codec}
    };
}

std::optional<std::string> movie_bk2_platform(std::string_view text) noexcept {
    constexpr std::string_view kKey = "Platform ";
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find('\n', at);
        std::string_view row = text.substr(
            at,
            (nl == std::string_view::npos ? text.size() : nl) - at
        );
        if (!row.empty() && row.back() == '\r')
            row.remove_suffix(1);
        if (row.size() > kKey.size() && row.starts_with(kKey)) {
            const std::string_view v = IMovieFormat::trim(row.substr(kKey.size()));
            if (v.empty() || v.size() > 32)
                return std::nullopt;
            return std::string(v);
        }
        if (nl == std::string_view::npos)
            break;
        at = nl + 1;
    }
    return std::nullopt;
}

[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(svc::IFile& movie) {
    const auto sz = movie.size();
    if (!sz)
        return std::unexpected(sz.error());
    const auto cap = static_cast<std::size_t>(
        std::min<std::uint64_t>(sz->v, MovieArchiveFormat::kHeadMax)
    );
    if (cap == 0)
        return std::optional<std::string>{};
    std::vector<char> raw(cap);
    const auto n = movie.read_at(0, std::as_writable_bytes(std::span(raw.data(), raw.size())));
    if (!n)
        return std::unexpected(n.error());
    if (*n == 0)
        return std::optional<std::string>{};
    return movie_bk2_platform(std::string_view(raw.data(), *n));
}

[[nodiscard]] Ex<std::optional<std::string>> movie_bk2_platform(
    const svc::Vfs& vfs,
    std::string_view movie_path
) {
    if (!infra::ieq(movie_path_extension(movie_path), "bk2"))
        return std::optional<std::string>{};
    auto f = open_movie_archive(vfs, movie_path, kBk2Archive);
    if (!f)
        return std::unexpected(f.error());
    return movie_bk2_platform(**f);
}

std::optional<MovieSystem> movie_system_for(
    const svc::Vfs& vfs,
    std::string_view movie_path
) noexcept {
    if (auto u = movie_system_for_path(movie_path))
        return u;
    if (!infra::ieq(movie_path_extension(movie_path), "bk2"))
        return std::nullopt;
    const auto plat = movie_bk2_platform(vfs, movie_path);
    if (!plat || !*plat)
        return std::nullopt;
    return system_for_platform(**plat, movie_path);
}

std::string movie_unplayable_reason(
    const Ex<std::optional<std::string>>& platform,
    std::string_view movie_path
) {
    if (!platform) {
        const Error& e = platform.error();
        if (e.code == Errc::io && e.detail == static_cast<std::uint32_t>(ENOSYS))
            return "ZIP support is not built in, so this .bk2 cannot be opened";
        return "this .bk2 archive is unreadable; check that the file is complete";
    }
    if (!platform->has_value())
        return "this .bk2 names no Platform line, so tasty cannot tell which console it is for";
    if (system_for_platform(**platform, movie_path))
        return {};
    return "this .bk2 is for " + **platform + "; tasty plays " + playable_platforms() + " movies";
}

std::string movie_unplayable_reason(const svc::Vfs& vfs, std::string_view movie_path) {
    if (!infra::ieq(movie_path_extension(movie_path), "bk2") || !vfs.file_exists(movie_path))
        return {};
    return movie_unplayable_reason(movie_bk2_platform(vfs, movie_path), movie_path);
}

[[nodiscard]] Ex<MovieFacts> read_movie_facts(
    const svc::Vfs& vfs,
    const IMovieFormat& format,
    std::string_view movie_path
) {
    auto f = format.open_movie(vfs, movie_path);
    if (!f)
        return std::unexpected(f.error());
    MovieFacts facts{};
    std::vector<char> buf(4096);
    std::string line;
    std::uint64_t off = 0;
    for (;;) {
        const auto got = (*f)->read_at(
            off,
            std::as_writable_bytes(std::span(buf.data(), buf.size()))
        );
        if (!got)
            return std::unexpected(got.error());
        if (*got == 0)
            break;
        off += *got;
        for (std::size_t i = 0; i < *got; ++i) {
            const char ch = buf[i];
            if (ch == '\n') {
                std::string_view row{line};
                if (!row.empty() && row.back() == '\r')
                    row.remove_suffix(1);
                if (format.starts_log(row)) {
                    if (auto d = format.finish_header(facts); !d)
                        return std::unexpected(d.error());
                    return facts;
                }
                auto next = format.header_line(row, facts);
                if (!next)
                    return std::unexpected(next.error());
                facts = *next;
                line.clear();
            } else if (line.size() < kMovieLineMax) {
                line.push_back(ch);
            }
        }
        if (*got < buf.size())
            break;
    }
    if (auto d = format.finish_header(facts); !d)
        return std::unexpected(d.error());
    return facts;
}

bool movie_system_supported(const MovieSystem& sys) noexcept {
    switch (sys.kind) {
        case CoreKind::Generic:
        case CoreKind::Snes:
        case CoreKind::MegaDrive:
        case CoreKind::Psx:
        case CoreKind::Menu:
            return sys.codec.has_value() || sys.kind == CoreKind::Menu;
        case CoreKind::MegaCd:
        case CoreKind::PceCd:
        case CoreKind::Sgb:
        case CoreKind::Arcade:
        case CoreKind::Electron:
        case CoreKind::AtariSt:
        case CoreKind::ThreeDo:
        case CoreKind::N64:
        case CoreKind::NeoGeo:
        case CoreKind::Ao486:
        case CoreKind::Pc110:
        case CoreKind::Z486:
        case CoreKind::Z386:
        case CoreKind::Pcxt:
        case CoreKind::Tandy1000:
        case CoreKind::Pcjr:
        case CoreKind::PcxtEga:
        case CoreKind::Uneon:
        case CoreKind::Saturn:
        case CoreKind::Cdi:
        case CoreKind::Count:
            return false;
    }

    return false;
}

bool movie_system_plays(std::string_view conf_str_name) noexcept {
    for (const MovieCodecRow& r : movie_codec_rows()) {
        if (r.name != conf_str_name)
            continue;
        if (movie_system_supported(
                MovieSystem{r.kind, r.name, infra::OptRef<const MovieCodec>{r.codec}}
            ))
            return true;
    }
    return false;
}

}
