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
#include "hal/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"

namespace mister::cores {

struct CoreFactory {

    const CoreProfile& profile;
    std::unique_ptr<Core> (*make)(const CoreProfile&, const CoreGrant&);

    MakeLadder make_ladder = nullptr;

    MakeLoader make_loader = nullptr;

    MakeCompanion make_companion = nullptr;

    infra::OptRef<const IManifestDocRole> manifest_doc{};
};

std::span<const CoreFactory> core_table();

enum class LoadHint : std::uint8_t { None, XmlManifest };

[[nodiscard]] infra::OptRef<const CoreFactory> find_core(std::string_view conf_str_name);

[[nodiscard]] infra::OptRef<const CoreFactory> find_core(
    std::string_view conf_str_name,
    LoadHint hint
);

infra::OptRef<const IManifestDocRole> manifest_doc_role();

const CoreProfile& profile_for(std::string_view conf_str_name);

const CoreProfile& profile_for(std::string_view conf_str_name, LoadHint hint);

[[nodiscard]] constexpr MakeLoader loader_for(
    infra::OptRef<const CoreFactory> row,
    hal::FabricAddr load_addr
) noexcept {
    if (row && row->make_loader != nullptr)
        return row->make_loader;
    const CoreProfile& p = row ? row->profile : kGenericProfile;
    if (load_addr == hal::FabricAddr{} || p.file_tx_whole)
        return nullptr;
    return &make_addressed_load;
}

}
