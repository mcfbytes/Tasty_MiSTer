// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/manifest_doc_role.h"
#include "cores/core_grant.h"
#include "cores/companion_load.h"
#include "cores/ladder_context.h"
#include "cores/loader_context.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "infra/error.h"

namespace mister::cores {

struct CoreFactory {
    CoreKind kind;
    std::string_view name;
    const CoreProfile* profile;
    std::unique_ptr<Core> (*make)(const CoreProfile&, const CoreGrant&);

    MakeLadder make_ladder = nullptr;

    MakeLoader make_loader = nullptr;

    MakeCompanion make_companion = nullptr;

    const ManifestDocRole* manifest_doc = nullptr;
};

std::span<const CoreFactory> core_table();

enum class LoadHint : std::uint8_t { None, XmlManifest };

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name);

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name, LoadHint hint);

const ManifestDocRole* manifest_doc_role();

const CoreProfile& profile_for(std::string_view conf_str_name);

}  // namespace mister::cores
