// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/movie_codec.h"

#include "cores/megadrive_bk2_format.h"
#include "cores/megadrive_gmv_format.h"
#include "cores/megadrive_movie_codec.h"
#include "cores/movie_archive.h"
#include "cores/nes_bk2_codec.h"
#include "cores/nes_fm2_codec.h"
#include "cores/nes_movie_codec.h"
#include "cores/psx_bk2_format.h"
#include "cores/snes_bk2_codec.h"
#include "cores/psx_movie_codec.h"
#include "cores/snes_lsmv_codec.h"
#include "infra/ascii.h"
#include "infra/parse_int.h"
#include "infra/path_parts.h"
#include "movie_codec_detail.h"
#include "svc/file.h"
#include "svc/vfs.h"

#include <limits>
#include <span>
#include <string>

namespace mister::cores {
namespace {

constexpr NesMovieCodec kNesCore{};
constexpr NesFm2Codec kNesFm2Format{};
constexpr NesBk2Codec kNesBk2Format{};
constexpr SnesMovieCodec kSnesCore{};
constexpr SnesLsmvCodec kSnesLsmvFormat{};
constexpr SnesBk2Codec kSnesBk2Format{};
constexpr MegaDriveMovieCodec kMegaDriveCore{};
constexpr MegaDriveBk2Format kMegaDriveBk2Format{};
constexpr MegaDriveGmvFormat kMegaDriveGmvFormat{};
constexpr PsxMovieCodec kPsxCore{};
constexpr PsxBk2Format kPsxBk2Format{};

constexpr std::array<MovieCodecRow, 7> kMovieCodecs{{
    {CoreKind::Generic, "NES", "fm2", "", "", {kNesCore, kNesFm2Format}},
    {CoreKind::Generic, "NES", "bk2", NesBk2Codec::kPlatformName, "NES", {kNesCore, kNesBk2Format}},
    {CoreKind::Snes, "SNES", "lsmv", "", "", {kSnesCore, kSnesLsmvFormat}},
    {CoreKind::Snes,
     "SNES",
     "bk2",
     SnesBk2Codec::kPlatformName,
     "SNES",
     {kSnesCore, kSnesBk2Format}},
    {CoreKind::MegaDrive,
     "MegaDrive",
     "bk2",
     MegaDriveBk2Format::kPlatformName,
     "Genesis",
     {kMegaDriveCore, kMegaDriveBk2Format}},
    {CoreKind::MegaDrive, "MegaDrive", "gmv", "", "", {kMegaDriveCore, kMegaDriveGmvFormat}},
    {CoreKind::Psx, "PSX", "bk2", PsxBk2Format::kPlatformName, "PSX", {kPsxCore, kPsxBk2Format}},
}};

consteval bool platform_column_holds() {
    for (const MovieCodecRow& r : kMovieCodecs) {
        const bool is_bk2 = r.ext == "bk2";
        if (is_bk2 != !r.bk2_platform.empty())
            return false;
        if (is_bk2 != !r.display_name.empty())
            return false;
    }
    return true;
}

static_assert(
    platform_column_holds(),
    "a .bk2 row carries its format's Platform value and its console's display name, and "
    "no other row carries either"
);

}

IMovieFormat::KeyValue IMovieFormat::split(std::string_view line) noexcept {
    line = trim(line);
    const std::size_t sp = line.find(' ');
    if (sp == std::string_view::npos)
        return {line, {}};
    return {line.substr(0, sp), trim(line.substr(sp + 1))};
}

std::string_view IMovieFormat::trim(std::string_view s) noexcept {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}

proto::FieldValue IReplayCore::SettingNeed::value_in(const proto::StatusWord& live) const noexcept {
    return live.field(field);
}

bool IReplayCore::SettingNeed::met_by(const proto::StatusWord& live) const noexcept {
    return allowed.holds(value_in(live));
}

std::optional<IReplayCore::SettingNeeds> IReplayCore::setting_needs_for(
    const MovieFacts& f,
    std::optional<RamFill> fill
) const noexcept {
    SettingNeeds needs = setting_needs(f);
    if (!fill)
        return needs;
    const auto ram = ram_fill_need(*fill);
    if (!ram)
        return std::nullopt;
    for (std::size_t i = 0; i < needs.n.size(); ++i) {
        if (needs.rows[i].name != ram->name)
            continue;
        needs.rows[i] = *ram;
        return needs;
    }
    needs.add(*ram);
    return needs;
}

std::optional<std::string_view> IReplayCore::check_settings(
    const proto::StatusWord& live,
    const MovieFacts& f
) const noexcept {
    const SettingNeeds needs = setting_needs(f);
    for (const SettingNeed& n : needs.view()) {
        if (!n.met_by(live))
            return n.name;
    }
    return std::nullopt;
}

Ex<std::unique_ptr<svc::IFile>> IMovieFormat::open_plain_or_archive(
    infra::OptRef<const MovieArchiveFormat> container,
    const svc::Vfs& vfs,
    std::string_view path
) {
    if (container)
        return open_movie_archive(vfs, path, *container);
    return vfs.open(path, svc::OpenMode::Read);
}

Ex<IMovieFormat::DigestSource> IMovieFormat::head_digest_source(
    const svc::Vfs& vfs,
    std::string_view rom
) const {
    auto f = vfs.open(rom, svc::OpenMode::Read);
    if (!f)
        return std::unexpected(f.error());
    const auto size = (*f)->size();
    if (!size)
        return std::unexpected(size.error());
    if (size->v > kRomMax)
        return std::unexpected(movie_refusal(MovieRefusal::TooLong, ERR_SITE()));
    std::array<std::uint8_t, kRomHead> head{};
    const auto got = (*f)->read_at(0, std::as_writable_bytes(std::span<std::uint8_t>(head)));
    if (!got)
        return std::unexpected(got.error());
    const auto span = rom_digest_span(std::span<const std::uint8_t>(head.data(), *got), *size);
    if (!span)
        return std::unexpected(movie_refusal(MovieRefusal::System, ERR_SITE()));
    return DigestSource{.file = std::string(rom), .span = *span, .prefix = {}};
}

std::optional<std::chrono::nanoseconds> IReplayCore::power_on_ns(
    const proto::StatusWord& live,
    const MovieFacts& f
) const noexcept {
    const PowerOn p = power_on(f);
    if (p.clocks.zero())
        return std::chrono::nanoseconds{0};
    if (!p.clock_hz.valid() || p.field.width > 4)
        return std::nullopt;
    if (p.field.valid() && !p.when.holds(live.field(p.field)))
        return std::chrono::nanoseconds{0};

    const auto ns = p.clocks.span(p.clock_hz);
    if (!ns || *ns > std::chrono::nanoseconds{std::numeric_limits<std::uint32_t>::max()})
        return std::nullopt;
    return ns;
}

std::optional<MovieRerecordCount> rerecord_count(
    std::string_view key,
    std::string_view val
) noexcept {
    if (key != "rerecordCount" && key != "rerecords" && key != "GmvRerecords")
        return std::nullopt;
    if (val.empty() || val.size() > 10)
        return std::nullopt;
    if (const auto count = infra::parse_int<std::uint32_t>(val))
        return MovieRerecordCount{*count};
    return std::nullopt;
}

std::span<const MovieCodecRow> movie_codec_rows() noexcept {
    return kMovieCodecs;
}

infra::OptRef<const MovieCodec> movie_codec_for(
    std::string_view conf_str_name,
    std::string_view movie_path
) noexcept {
    const std::string_view ext = infra::extension_bare(movie_path);
    for (const MovieCodecRow& r : movie_codec_rows()) {
        if (r.name == conf_str_name && infra::ieq(r.ext, ext))
            return infra::OptRef<const MovieCodec>{r.codec};
    }
    return {};
}

const MovieCodecRow* unique_row_for_ext(std::string_view movie_path) noexcept {
    const std::string_view ext = infra::extension_bare(movie_path);
    const MovieCodecRow* found = nullptr;
    unsigned n = 0;
    for (const MovieCodecRow& r : movie_codec_rows()) {
        if (!infra::ieq(r.ext, ext))
            continue;
        found = &r;
        ++n;
    }
    return n == 1 ? found : nullptr;
}

infra::OptRef<const MovieCodec> movie_codec_for_path(std::string_view movie_path) noexcept {
    const MovieCodecRow* row = unique_row_for_ext(movie_path);
    if (row == nullptr)
        return {};
    return infra::OptRef<const MovieCodec>{row->codec};
}

}
