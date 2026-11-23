// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/atari8/atari8_decl.h"
#include "cores/boot_asset.h"
#include "cores/core_grant.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/host_bit_row.h"
#include "cores/loader.h"
#include "cores/loader_context.h"
#include "cores/mailbox_census_rows.h"
#include "cores/mailbox_servant_row.h"
#include "proto/mailbox_programme.h"

namespace mister::cores::manifests {

extern const CoreProfile kAtari800;
extern const CoreProfile kAtari5200;

inline constexpr auto kAtari800Services = std::to_array<LinkDecoderDecl>({
    {
        "atari800.mailbox",
        reactor::Cause::Tick,
        &kMailboxTick<kAtari800>,
        reactor::kEveryRound,
        RoundClaim::Never,
    },
});
inline constexpr auto kAtari5200Services = std::to_array<LinkDecoderDecl>({
    {
        "atari5200.mailbox",
        reactor::Cause::Tick,
        &kMailboxTick<kAtari5200>,
        reactor::kEveryRound,
        RoundClaim::Never,
    },
});

using atari8::kAtari800Mailbox;
using atari8::kAtari8Mailbox;

inline constexpr auto kAtari800HostBits = std::to_array<HostBitRow>({
    {.fact = HostFact::StartRomInhibit,
     .bits = {proto::StatusBit{41}, 1},
     .dir = HostBitRow::Dir::Read},
});

inline constexpr auto kAtari800StartAssets = std::to_array<BootAsset>({
    {.name = "boot0.rom",
     .where = kCoreHomeDir,
     .subdir = "ATARI800",
     .dest = WideIoIndex::of(IoIndex{}, 0),
     .unless = HostFact::StartRomInhibit},
    {.name = "boot1.rom",
     .where = kCoreHomeDir,
     .subdir = "ATARI800",
     .dest = WideIoIndex::of(IoIndex{}, 1),
     .unless = HostFact::StartRomInhibit},
    {.name = "boot2.rom",
     .where = kCoreHomeDir,
     .subdir = "ATARI800",
     .dest = WideIoIndex::of(IoIndex{}, 2),
     .unless = HostFact::StartRomInhibit},
    {.name = "boot3.rom",
     .where = kCoreHomeDir,
     .subdir = "ATARI800",
     .dest = WideIoIndex::of(IoIndex{}, 3)},
    {.name = "sid_data.bin",
     .where = kCoreHomeDir,
     .subdir = "ATARI800",
     .dest = WideIoIndex::of(IoIndex{7}, 0)},
});

inline constexpr auto kAtari5200StartAssets = std::to_array<BootAsset>({
    {.name = "boot0.rom",
     .where = kCoreHomeDir,
     .subdir = "ATARI5200",
     .dest = WideIoIndex::of(IoIndex{}, 0)},
});

inline constexpr CoreProfile kAtari800{
    .kind = CoreKind::Atari800,
    .name = "ATARI800",
    .rbf = "",
    .services = kAtari800Services,
    .slots = {},
    .boot_assets = {},
    .start_assets = kAtari800StartAssets,
    .start = StartRule::Assets,
    .signatures = {},
    .host_bits = kAtari800HostBits,
    .undeclared_start_mount = StartMount::Unmodelled,

    .grants = {Grant::Mailbox},
};

inline constexpr CoreProfile kAtari5200{
    .kind = CoreKind::Atari5200,
    .name = "ATARI5200",
    .rbf = "",
    .services = kAtari5200Services,
    .slots = {},
    .boot_assets = {},
    .start_assets = kAtari5200StartAssets,
    .start = StartRule::Assets,
    .signatures = {},
    .undeclared_start_mount = StartMount::Unmodelled,
    .grants = {Grant::Mailbox},
};

std::unique_ptr<Core> make_atari800(const CoreProfile& p, const CoreGrant& g);
std::unique_ptr<Core> make_atari5200(const CoreProfile& p, const CoreGrant& g);
std::unique_ptr<IMailboxServant> make_atari800_servant(const ServantGrant& g);
std::unique_ptr<IMailboxServant> make_atari5200_servant(const ServantGrant& g);
std::unique_ptr<ILoader> make_atari800_loader(const LoaderContext& ctx);
std::unique_ptr<ILoader> make_atari5200_loader(const LoaderContext& ctx);

inline constexpr MailboxServantRow kAtari800Servant{
    .id = ServantId::Atari8Computer,
    .make = &make_atari800_servant
};
inline constexpr MailboxServantRow kAtari5200Servant{
    .id = ServantId::Atari8Console,
    .make = &make_atari5200_servant
};

}
