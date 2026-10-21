// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/arcade_manifest_role.h"

#include <memory>
#include <string_view>
#include <utility>

#include "cores/mra_cheat_records.h"
#include "cores/mra_doc.h"
#include "cores/mra_stager.h"

namespace mister::cores {

std::uint64_t ArcadeManifestRole::doc_max() const noexcept {
    return mra::kMraDocMax;
}

std::string_view ArcadeManifestRole::root_of(std::string_view manifest_rel) const noexcept {
    return mra::arcade_root_of(manifest_rel);
}

ManifestDocFacts ArcadeManifestRole::facts_of(std::string_view doc) const {
    mra::MraDoc parsed;
    parsed.parse(doc);
    return ManifestDocFacts{
        .vertical = parsed.vertical,
        .rotation = parsed.rotation,
        .setname = std::move(parsed.setname),
        .setname_same_dir = parsed.setname_same_dir
    };
}

std::unique_ptr<ICheatRecords> ArcadeManifestRole::cheats_of(std::string_view doc) const {
    mra::MraDoc parsed;
    if (!doc.empty())
        parsed.parse(doc);
    return std::make_unique<mra::MraCheatRecords>(std::move(parsed.cheats), parsed.cheat_geometry);
}

std::unique_ptr<IManifestStager> ArcadeManifestRole::stager_of(
    std::string_view doc,
    std::string_view manifest_rel,
    const svc::Vfs& vfs
) const {
    return mra::MraStager::of(doc, manifest_rel, vfs);
}

constinit const ArcadeManifestRole kMraRole{};

}
