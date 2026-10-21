// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "cores/cheat_records.h"
#include "cores/manifest_doc_facts.h"
#include "cores/manifest_stager.h"
#include "svc/vfs.h"

namespace mister::cores {

class IManifestDocRole {
public:
    [[nodiscard]] virtual std::uint64_t doc_max() const noexcept = 0;
    [[nodiscard]] virtual std::string_view root_of(
        std::string_view manifest_rel
    ) const noexcept = 0;
    [[nodiscard]] virtual ManifestDocFacts facts_of(std::string_view doc) const = 0;

    [[nodiscard]] virtual std::unique_ptr<ICheatRecords> cheats_of(std::string_view doc) const = 0;

    [[nodiscard]] virtual std::unique_ptr<IManifestStager> stager_of(
        std::string_view doc,
        std::string_view manifest_rel,
        const svc::Vfs& vfs
    ) const = 0;

protected:
    ~IManifestDocRole() = default;
    constexpr IManifestDocRole() = default;
    IManifestDocRole(const IManifestDocRole&) = default;
    IManifestDocRole& operator=(const IManifestDocRole&) = default;
};

}
