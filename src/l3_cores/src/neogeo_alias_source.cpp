// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo_alias_source.h"

#include <cstddef>
#include <span>

#include "cores/neo_header.h"
#include "cores/romset_files.h"
#include "cores/romset_plan.h"
#include "infra/ascii.h"
#include "infra/byte_view.h"

namespace mister::cores {

namespace {

constexpr std::string_view kIndexName = "romsets.xml";
constexpr std::string_view kSidecarName = "romset.xml";
constexpr std::string_view kHeaderExt = ".neo";

std::string join(std::string_view dir, std::string_view name) {
    if (dir.empty())
        return std::string(name);
    std::string p(dir);
    p += '/';
    p += name;
    return p;
}

}

void NeoGeoAliasSource::ensure_index(IRomsetFiles& files, BrowseDirs dirs) {
    if (indexed_ && indexed_dir_ == dirs.dir)
        return;
    auto xml = files.read_prefix(join(dirs.dir, kIndexName), kDocMaxBytes);
    if (!xml)
        xml = files.read_prefix(join(dirs.home, kIndexName), kDocMaxBytes);
    index_.scan(xml ? std::string_view(*xml) : std::string_view{});
    indexed_dir_ = std::string(dirs.dir);
    indexed_ = true;
    ++scans_;
}

RomsetAlias NeoGeoAliasSource::query(IRomsetFiles& files, BrowseDirs dirs, EntryLookup entry) {
    TASTY_SEAT_BODY(NeoGeoAliasSource);
    if (infra::iends_with(entry.raw_name, kHeaderExt)) {

        const auto bytes = files.read_prefix(
            join(dirs.dir, entry.raw_name),
            neogeo::kNeoHeaderBytes
        );
        if (!bytes)
            return RomsetNoMatch{};
        const auto hdr = neogeo::parse_neo_header(infra::text_bytes<const std::uint8_t>(*bytes));
        if (!hdr)
            return RomsetNoMatch{};
        return RomsetRetype{std::string(neogeo::name_view(*hdr))};
    }
    if (const auto xml = files.read_prefix(
            join(join(dirs.dir, entry.raw_name), kSidecarName),
            kDocMaxBytes
        )) {
        std::string alias = neogeo::sidecar_altname(*xml);
        if (!alias.empty())
            return RomsetRetype{std::move(alias)};
    }
    ensure_index(files, dirs);
    return index_.lookup(entry.key);
}

}
