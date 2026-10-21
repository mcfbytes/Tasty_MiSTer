// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_asset_finder.h"

#include <dirent.h>

#include <optional>
#include <string>
#include <utility>

#include "infra/ascii.h"
#include "vfs_detail.h"
#include "vfs_stat.h"
#include "vfs_zip.h"

namespace mister::svc {

using detail::compose;
using detail::ifind;
using detail::path_is_regular;
using detail::zip_archive_opens;
using infra::iends_with;

namespace {

bool valid_asset(const std::string& p, bool require_zip = false) {
    if (!path_is_regular(p))
        return false;
    if (!require_zip)
        return true;
    return zip_archive_opens(p);
}

std::optional<std::string> asset_in_same_dir(const std::string& rom_full, std::string_view ext) {
    const std::size_t slash = rom_full.rfind('/');
    if (slash == std::string::npos)
        return std::nullopt;
    const std::string dir = rom_full.substr(0, slash);

    ::DIR* d = ::opendir(dir.c_str());
    if (d == nullptr)
        return std::nullopt;
    std::string best;
    while (const ::dirent* de = ::readdir(d)) {
        const std::string_view name(de->d_name);
        if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN)
            continue;
        if (!iends_with(name, ext))
            continue;
        if (!best.empty() && !(name < std::string_view(best)))
            continue;

        if (de->d_type == DT_UNKNOWN && !path_is_regular(dir + "/" + std::string(name))) {
            continue;
        }
        best = name;
    }
    ::closedir(d);
    if (best.empty())
        return std::nullopt;
    return dir + "/" + best;
}

std::optional<std::string> asset_by_crc(Crc32 crc, std::string_view ext, const std::string& dir) {
    if (crc.v == 0)
        return std::nullopt;
    ::DIR* d = ::opendir(dir.c_str());
    if (d == nullptr)
        return std::nullopt;
    std::string best;
    while (const ::dirent* de = ::readdir(d)) {
        const std::string_view name(de->d_name);
        if (de->d_type != DT_REG && de->d_type != DT_UNKNOWN)
            continue;
        const auto parsed = Vfs::parse_crc_name(name, ext);
        if (!parsed || !(*parsed == crc))
            continue;
        if (!best.empty() && !(name < std::string_view(best)))
            continue;
        if (de->d_type == DT_UNKNOWN && !path_is_regular(dir + "/" + std::string(name))) {
            continue;
        }
        best = name;
    }
    ::closedir(d);
    if (best.empty())
        return std::nullopt;
    return dir + "/" + best;
}

std::string retarget_extension(std::string p, std::string_view infix, std::string_view ext) {
    const std::size_t dot = p.rfind('.');
    if (dot != std::string::npos)
        p.resize(dot);
    p.append(infix);
    p.append(ext);
    return p;
}

}

Ex<std::string> GameAssetFinder::find(const Vfs::AssetQuery& q) const {

    const bool cd_layout = !q.cd_asset_dir.empty();

    const std::string core_dir = compose(root_path_, q.core_dir);
    const std::string cd_dir = cd_layout ? compose(root_path_, q.cd_asset_dir) : core_dir;

    std::string path;
    if (ifind(q.rom_path, q.ext) == std::string_view::npos) {
        path = retarget_extension(compose(root_path_, q.rom_path), "", q.ext);
    }
    if (!path.empty() && valid_asset(path, q.require_zip))
        return path;

    if (cd_layout) {
        const std::string rom_full = compose(root_path_, q.rom_path);
        if (auto p = asset_in_same_dir(rom_full, q.ext)) {
            if (valid_asset(*p, q.require_zip))
                return *p;
        }
    }

    const std::size_t slash = q.rom_path.rfind('/');
    if (slash != std::string_view::npos) {
        std::string cand(cd_dir);
        cand.append(q.rom_path.substr(slash));
        cand = retarget_extension(std::move(cand), cd_layout ? " []" : "", q.ext);
        if (valid_asset(cand, q.require_zip))
            return cand;
    }

    if (auto p = asset_by_crc(q.crc, q.ext, core_dir)) {
        if (valid_asset(*p, q.require_zip))
            return *p;
    }

    return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
}

}
