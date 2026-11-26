// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <memory>

#include "cores/boot_ladder.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core.h"
#include "cores/core_window_decl.h"
#include "cores/disk_servant.h"
#include "cores/disk_servant_row.h"
#include "cores/file_slot.h"
#include "cores/ladder_context.h"
#include "cores/minimig/minimig_config_codec.h"
#include "cores/minimig/minimig_config_layout.h"
#include "cores/mailbox_census_rows.h"
#include "cores/mailbox_servant_row.h"
#include "cores/minimig/minimig_core.h"
#include "cores/minimig/minimig_option_rows.h"
#include "cores/minimig/share_wire.h"
#include "cores/sector_kick_row.h"
#include "cores/servant_grant.h"
#include "cores/share_host.h"
#include "cores/share_row.h"
#include "infra/opt_ref.h"
#include "proto/disk_protocol.h"
#include "svc/disk_codec.h"
#include "svc/disk_format_row.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::manifests {

extern const CoreProfile kMinimig;

inline constexpr auto kMinimigServices = std::to_array<LinkDecoderDecl>({
    {
        "minimig.floppy_poll",
        reactor::Cause::Tick,
        &kSectorKick<kMinimig>,
        std::chrono::milliseconds{1},
        RoundClaim::Always,
    },
    {
        "minimig.mailbox",
        reactor::Cause::Tick,
        &kMailboxTick<kMinimig>,
        reactor::kEveryRound,
        RoundClaim::Never,
    },
});

inline constexpr proto::FileSize kMinimigShareBytes = proto::FileSize::of(minimig::kSharePageBytes);

inline constexpr auto kMinimigWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x07FF'4000u, kMinimigShareBytes.v, "minimig-share"}, .feeds_pcm = false},
});

inline constexpr ShareRow kMinimigShareRow{
    .page = {},
    .bytes = kMinimigShareBytes,
    .poll = infra::ParkTimeout{1},
    .default_dir = minimig::kShareDefaultDir,
};

inline constexpr auto kMinimigDiskFormats = std::to_array<svc::DiskFormatRow>({
    {.ext = {},
     .size_bytes = 0,
     .tolerance = 0,
     .magic = {},
     .codec = svc::DiskCodec::Adf,
     .why = "minimig_fdd.cpp:630-648: InsertFloppy opens the picked file and serves its whole "
            "tracks as MFM"},
});

inline constexpr auto kMinimigSlots = std::to_array<FileSlot>({
    {.index = IoIndex{0},
     .extensions = "ADF",
     .label = "df0",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = IoIndex{1},
     .extensions = "ADF",
     .label = "df1",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = IoIndex{2},
     .extensions = "ADF",
     .label = "df2",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = IoIndex{3},
     .extensions = "ADF",
     .label = "df3",
     .writable = true,
     .role = SlotRole::Ladder},
    {.index = minimig::MinimigCore::kKickstartSlot,
     .extensions = "ROM",
     .label = "Kickstart",
     .writable = false,
     .role = SlotRole::Ladder},
    {.index = minimig::MinimigCore::kExtRomSlot,
     .extensions = "ROM",
     .label = "Ext. ROM",
     .writable = false,
     .role = SlotRole::Ladder},
});

inline constexpr CoreProfile kMinimig{
    .kind = CoreKind::Minimig,
    .name = "Minimig",
    .rbf = "Minimig",
    .services = kMinimigServices,
    .slots = kMinimigSlots,
    .boot_assets = {},
    .start = StartRule::None,
    .signatures = {},

    .suppress_reset_status_write = true,

    .block_service = BlockService::CoreOwn,

    .disk_formats = kMinimigDiskFormats,

    .option_rows = minimig::kMinimigOptionRows,
    .option_pages = minimig::kMinimigOptionPages,

    .windows = kMinimigWindows,

    .no_stream_role = true,

    .reset =
        {.button_reacts = true,
         .action_reacts = true,
         .ladders = true,
         .restore_boots = true,
         .reapplies_live_config = true},

    .host = {.input_sink = InputSinkId::PacedKeycodes},

    .config_codec = infra::OptRef<const IConfigCodec>{minimig::kMinimigConfigCodec},

    .grants = {Grant::Mailbox, Grant::Disk},
    .home_dir = minimig::mm_cfg::kHomeDir,
    .rtc_refresh = std::chrono::seconds{60},

    .share = kMinimigShareRow,
};

std::unique_ptr<Core> make_minimig(const CoreProfile& p, const CoreGrant& g);

std::unique_ptr<IMailboxServant> make_minimig_cd_servant(const ServantGrant& g);
inline constexpr MailboxServantRow kMinimigCdServant{
    .id = ServantId::MinimigCd,
    .make = &make_minimig_cd_servant
};

std::unique_ptr<BootLadder> make_minimig_ladder(const CoreProfile& p, const LadderContext& ctx);

std::unique_ptr<IShareHost> make_minimig_share_host(const svc::Vfs& vfs, std::string_view root);

std::unique_ptr<IDiskServant> make_minimig_floppy_servant(
    const CoreProfile& p,
    const ServantGrant& g
);
inline constexpr auto kMinimigDiskServants = std::to_array<DiskServantRow>({
    {.protocol = proto::DiskProtocol::AmigaFloppy, .make = &make_minimig_floppy_servant},
});

}
