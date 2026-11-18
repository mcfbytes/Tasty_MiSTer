// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/map_store.h"
#include "svc/button_map.h"
#include "svc/device_identity.h"
#include "svc/vfs.h"

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
    auto got = (*file)->read_at(proto::kFileStart, out.blob);
    if (!got)
        return std::unexpected(got.error());
    if (*got != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(*got)}
        );
    }
    return out;
}

Ex<WireWords> MapStore::load_joy_words(
    std::string_view core,
    const DeviceIdentity& id,
    bool mod
) const {
    auto map = load(core, id, MapKind::Joystick, mod);
    if (!map)
        return std::unexpected(map.error());
    return map->words();
}

Ex<MapStore::Write> MapStore::encode(
    std::string_view core,
    const DeviceIdentity& id,
    const ButtonMap& map,
    bool mod
) {
    const std::size_t want = map_spec(map.kind).bytes;
    if (map.blob.size() != want) {

        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(map.blob.size())}
        );
    }
    const std::string name = filename(core, id, map.kind, mod);
    const rules::MapPaths paths = rules::map_paths(name, map.kind);

    Write w{};

    if (!paths.fallback.empty()) {
        w.retire.assign("config/");
        w.retire.append(paths.fallback);
    }

    w.rel.assign("config/");
    w.rel.append(paths.primary);
    w.bytes = std::span<const std::byte>{map.blob};
    return w;
}

}
