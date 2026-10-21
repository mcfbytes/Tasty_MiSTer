// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neogeo_alias_source.h"

#include <cctype>
#include <cstddef>
#include <span>

#include "cores/neo_file.h"
#include "cores/romset_files.h"
#include "infra/byte_view.h"

namespace mister::cores {

namespace {

constexpr std::string_view kIndexName = "romsets.xml";
constexpr std::string_view kSidecarName = "romset.xml";
constexpr std::string_view kHeaderExt = ".neo";

bool iends_with(std::string_view s, std::string_view suffix) noexcept {
    if (suffix.size() > s.size())
        return false;
    const std::string_view tail = s.substr(s.size() - suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(tail[i]))
            != std::tolower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

std::string join(std::string_view dir, std::string_view name) {
    if (dir.empty())
        return std::string(name);
    std::string p(dir);
    p += '/';
    p += name;
    return p;
}

}

void NeoGeoAliasSource::ensure_index(
    IRomsetFiles& files,
    std::string_view dir,
    std::string_view home
) {
    if (indexed_ && indexed_dir_ == dir)
        return;
    auto xml = files.read_prefix(join(dir, kIndexName), kDocMaxBytes);
    if (!xml)
        xml = files.read_prefix(join(home, kIndexName), kDocMaxBytes);
    index_.scan(xml ? std::string_view(*xml) : std::string_view{});
    indexed_dir_ = std::string(dir);
    indexed_ = true;
    ++scans_;
}

RomsetAlias NeoGeoAliasSource::query(
    IRomsetFiles& files,
    std::string_view dir,
    std::string_view home,
    std::string_view entry,
    std::string_view key
) {
    TASTY_SEAT_BODY(NeoGeoAliasSource);
    if (iends_with(entry, kHeaderExt)) {

        const auto bytes = files.read_prefix(join(dir, entry), neogeo::kNeoHeaderBytes);
        if (!bytes)
            return {};
        const auto hdr = neogeo::parse_neo_header(infra::text_bytes<const std::uint8_t>(*bytes));
        if (!hdr)
            return {};
        return RomsetAlias{.drop = false, .alias = std::string(neogeo::name_view(*hdr))};
    }
    if (const auto xml = files.read_prefix(join(join(dir, entry), kSidecarName), kDocMaxBytes)) {
        std::string alias = neogeo::sidecar_altname(*xml);
        if (!alias.empty())
            return RomsetAlias{.drop = false, .alias = std::move(alias)};
    }
    ensure_index(files, dir, home);
    return index_.lookup(key);
}

}
