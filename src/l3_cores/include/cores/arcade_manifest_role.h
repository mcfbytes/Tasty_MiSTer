// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/cheat_records.h"
#include "cores/manifest_doc_facts.h"
#include "cores/manifest_doc_role.h"

namespace mister::cores {

class ArcadeManifestRole final : public IManifestDocRole {
public:
    constexpr ArcadeManifestRole() = default;

    [[nodiscard]] std::uint64_t doc_max() const noexcept override;
    [[nodiscard]] std::string_view root_of(std::string_view manifest_rel) const noexcept override;
    [[nodiscard]] ManifestDocFacts facts_of(std::string_view doc) const override;
    [[nodiscard]] ManifestDocFacts facts_at(
        std::string_view text,
        std::string_view path,
        const svc::Vfs& vfs
    ) const override;
    [[nodiscard]] std::unique_ptr<ICheatRecords> cheats_of(std::string_view doc) const override;
    [[nodiscard]] std::unique_ptr<IManifestStager> stager_of(
        std::string_view doc,
        std::string_view manifest_rel,
        const svc::Vfs& vfs
    ) const override;
};

extern const ArcadeManifestRole kMraRole;

}
