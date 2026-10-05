// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "cores/companion_load.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/mailbox_census_rows.h"
#include "proto/mailbox_frame_decl.h"

namespace mister::cores::manifests {

extern const CoreProfile kSnes;

inline constexpr auto kSnesServices = std::to_array<LinkDecoderDecl>({
    {
        "snes.mailbox",
        reactor::Cause::Tick,
        &kMailboxTick<kSnes>,
        0,
        DeadlineClass::A,
        OsdBudget::Shared,
    },
});

inline constexpr proto::MailboxFrameDecl kSnesMailboxFrame{.poll_opcode = 0x34, .words = 4};

inline constexpr auto kSnesWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x0060'0000u, 0u, "snes-msu-data"}, .feeds_pcm = false},
});

inline constexpr auto kSnesStartAssets = stock_start_chain("SNES");

inline constexpr CoreProfile kSnes{
    .kind = CoreKind::Snes,
    .name = "SNES",
    .rbf = "",
    .services = kSnesServices,
    .slots = {},
    .boot_assets = {},
    .start_assets = kSnesStartAssets,
    .signatures = {},
    .windows = kSnesWindows,
    .file_tx_whole = true,
};

std::unique_ptr<Core> make_snes(const CoreProfile& p, const HostServices& h);

std::unique_ptr<ICompanionLoad> make_snes_companion(const svc::Vfs& vfs);

}  // namespace mister::cores::manifests
