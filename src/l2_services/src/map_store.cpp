// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/map_store.h"
#include "svc/button_map.h"
#include "svc/device_identity.h"
#include "svc/vfs.h"

#include <unistd.h>

#include <cerrno>
#include <utility>

#include "input_rules.h"
#include "input_service_detail.h"

namespace mister::svc {

std::string MapStore::filename(
    std::string_view core,
    const DeviceIdentity& id,
    MapKind kind,
    bool mod
) {
    return rules::map_filename(core, id, kind, mod);
}

Ex<ButtonMap> MapStore::load(
    std::string_view core,
    const DeviceIdentity& id,
    MapKind kind,
    bool mod
) const {
    if (!vfs_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    const std::string name = filename(core, id, kind, mod);
    const rules::MapPaths paths = rules::map_paths(name, kind);
    const std::size_t want = map_spec(kind).bytes;

    static constexpr SearchDir kConfig[] = {SearchDir::ConfigDir};
    const SearchPolicy policy{kConfig, true};

    std::string resolved;
    if (auto r = vfs_->resolve(paths.primary, policy); r) {
        resolved = std::move(*r);
    } else if (!paths.fallback.empty()) {
        auto f = vfs_->resolve(paths.fallback, policy);
        if (!f)
            return std::unexpected(f.error());
        resolved = std::move(*f);
    } else {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    auto file = vfs_->open(resolved, OpenMode::ReadWhole);
    if (!file)
        return std::unexpected(file.error());

    ButtonMap out{};
    out.kind = kind;
    out.blob.resize(want);
    auto got = (*file)->read_at(0, out.blob);
    if (!got)
        return std::unexpected(got.error());
    if (*got != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(*got)}
        );
    }
    return out;
}

Ex<void> MapStore::save(
    std::string_view core,
    const DeviceIdentity& id,
    const ButtonMap& map,
    bool mod
) const {
    if (!vfs_) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    const std::size_t want = map_spec(map.kind).bytes;
    if (map.blob.size() != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(map.blob.size())}
        );
    }
    const std::string name = filename(core, id, map.kind, mod);
    const rules::MapPaths paths = rules::map_paths(name, map.kind);

    const std::string root = vfs_->root_path() + "/config/";

    if (!paths.fallback.empty()) {
        const std::string stale = root + paths.fallback;
        if (::unlink(stale.c_str()) != 0 && errno != ENOENT) {
            return std::unexpected(os_error(ERR_SITE()));
        }
    }

    const std::string dest = root + paths.primary;

    {
        std::string rel("config/");
        rel.append(paths.primary);
        const auto slash = rel.rfind('/');
        if (auto r = vfs_->ensure_dir(rel.substr(0, slash)); !r)
            return r;
    }

    const std::string tmp = dest + ".tmp";
    {
        auto file = vfs_->open(tmp, OpenMode::Truncate);
        if (!file)
            return std::unexpected(file.error());
        auto put = (*file)->write_at(0, map.blob);
        if (!put)
            return std::unexpected(put.error());
        if (*put != map.blob.size()) {
            return std::unexpected(
                Error{Errc::short_write, ERR_SITE(), static_cast<std::uint32_t>(*put)}
            );
        }
        if (auto f = (*file)->flush(); !f)
            return f;
    }
    return vfs_->replace(tmp, dest);
}

}
