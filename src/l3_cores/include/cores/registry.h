// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/addressed_load.h"
#include "cores/manifest_doc_role.h"
#include "cores/core_grant.h"
#include "cores/companion_load.h"
#include "cores/ladder_context.h"
#include "cores/loader_context.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/generic_profile.h"
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

    const IManifestDocRole* manifest_doc = nullptr;
};

std::span<const CoreFactory> core_table();

enum class LoadHint : std::uint8_t { None, XmlManifest };

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name);

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name, LoadHint hint);

const IManifestDocRole* manifest_doc_role();

const CoreProfile& profile_for(std::string_view conf_str_name);

[[nodiscard]] constexpr MakeLoader loader_for(const CoreFactory* row,
                                              std::uint32_t load_addr) noexcept {
    if (row != nullptr && row->make_loader != nullptr) return row->make_loader;
    const CoreProfile& p = row != nullptr ? *row->profile : kGenericProfile;
    if (load_addr == 0 || p.file_tx_whole) return nullptr;
    return &make_addressed_load;
}

}  // namespace mister::cores
