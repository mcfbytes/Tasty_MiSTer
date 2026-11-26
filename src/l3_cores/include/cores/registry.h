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
#include "cores/core.h"
#include "cores/disk_servant_row.h"
#include "cores/mailbox_servant_row.h"
#include "cores/save_store_column.h"
#include "cores/share_host.h"
#include "cores/slot_servant_row.h"

#include "cores/sidecar_builder.h"
#include "cores/generic_profile.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/nic_personality.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

using MakeShareHost = std::unique_ptr<IShareHost> (*)(const svc::Vfs& vfs, std::string_view root);

struct CoreFactory {

    const CoreProfile& profile;
    std::unique_ptr<Core> (*make)(const CoreProfile&, const CoreGrant&);

    MakeLadder make_ladder = nullptr;

    MakeLoader make_loader = nullptr;

    bool derives_content = false;

    MakeCompanion make_companion = nullptr;

    MakeSidecar make_sidecar = nullptr;

    MakeSaveStore make_save_store = nullptr;

    std::span<const DiskServantRow> disk_servants{};

    MailboxServantRow mailbox_servant{};

    std::span<const SlotServantRow> slot_servants{};

    infra::OptRef<const IManifestDocRole> manifest_doc{};

    svc::MakeNic make_nic = nullptr;

    MakeShareHost make_share_host = nullptr;

    [[nodiscard]] constexpr bool saves_by_channel() const noexcept {
        return make_save_store != nullptr;
    }

    [[nodiscard]] constexpr bool serves_disks() const noexcept { return !disk_servants.empty(); }

    [[nodiscard]] constexpr bool has_mailbox_servant() const noexcept {
        return mailbox_servant.declared();
    }

    [[nodiscard]] constexpr bool serves_slots() const noexcept { return !slot_servants.empty(); }

    [[nodiscard]] constexpr bool has_ladder() const noexcept { return make_ladder != nullptr; }

    [[nodiscard]] constexpr bool has_loader() const noexcept { return make_loader != nullptr; }

    [[nodiscard]] constexpr bool has_nic() const noexcept { return make_nic != nullptr; }

    [[nodiscard]] constexpr bool serves_share() const noexcept {
        return make_share_host != nullptr;
    }

    [[nodiscard]] constexpr bool streams_files() const noexcept { return !profile.no_stream_role; }
};

std::span<const CoreFactory> core_table();

[[nodiscard]] proto::FileSize largest_declared_save() noexcept;

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
    if (row && row->has_loader())
        return row->make_loader;
    const bool streams = row ? row->streams_files() : !kGenericProfile.no_stream_role;
    if (load_addr == hal::FabricAddr{} || !streams)
        return nullptr;
    return &make_addressed_load;
}

}
