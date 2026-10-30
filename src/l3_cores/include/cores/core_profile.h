// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cores/blank_region.h"
#include "cores/blank_save_spec.h"
#include "cores/boot_asset.h"
#include "cores/cheat_lookup.h"
#include "cores/core_init_step.h"
#include "cores/core_window_decl.h"
#include "cores/file_slot.h"
#include "cores/host_policy.h"
#include "cores/option_row.h"
#include "cores/reset_row.h"
#include "cores/signature_row.h"
#include "cores/staging_policy.h"
#include "cores/start_rule.h"
#include "infra/error.h"
#include "proto/sector_floor.h"
#include "reactor/link_decoder_decl.h"
#include "svc/disk_format_row.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::svc {
class IAnalogReshape;
class ISectorProgram;
}

namespace mister::cores {

using reactor::RoundClaim;
using reactor::LinkDecoderDecl;

enum class CoreKind : std::uint8_t {
    Menu,
    Generic,
    MegaCd,
    PceCd,
    Psx,
    Snes,
    Sgb,
    Arcade,
    Electron,
    AtariSt,
    ThreeDo,
    MegaDrive,
    N64,
    NeoGeo,
    Ao486,
    Pc110,
    Z486,
    Z386,
    Pcxt,
    Tandy1000,
    Pcjr,
    PcxtEga,
    Uneon,
    Saturn,
    Cdi,
    Count,
};

enum class BlockService : std::uint8_t { Generic, CoreOwn };

template <class T>
struct SizeRow {
    std::uint32_t size_bytes;
    std::uint32_t tolerance;
    T value;
    std::string_view why;
};

template <class T>
constexpr const T* size_dispatch(std::span<const SizeRow<T>> rows, std::uint32_t size) {
    for (const auto& r : rows) {
        const std::uint32_t lo = r.size_bytes - r.tolerance;
        const std::uint32_t hi = r.size_bytes + r.tolerance;
        if (size >= lo && size <= hi)
            return &r.value;
    }
    return nullptr;
}

struct CoreProfile {
    TASTY_SEAT_EXEMPT(const_shared);
    CoreKind kind;
    std::string_view name;
    std::string_view rbf;

    std::span<const LinkDecoderDecl> services;

    std::span<const FileSlot> slots;
    std::span<const BootAsset> boot_assets;

    std::span<const BootAsset> start_assets{};

    StartRule start = StartRule::Chain;
    std::span<const SignatureRow> signatures;
    BlankSaveSpec blank_save{};

    bool saves_by_channel = false;

    bool suppress_reset_status_write = false;
    bool skips_memsz = false;

    StagingPolicy staging{};

    std::span<const CoreInitStep> init_additions{};

    bool suppress_analog_followup = false;

    infra::OptRef<const svc::IAnalogReshape> analog_reshape{};

    bool suppress_stick_dpad = false;

    bool is_front_end = false;

    BlockService block_service = BlockService::Generic;
    std::uint8_t block_drain_budget = 4;

    proto::SectorFloor block_sector_floor{};

    bool suppresses_mgl = false;

    std::uint32_t default_uart_baud = 115200;

    std::string_view cue_browse_dir{};

    bool image_rows_no_zip = false;

    CheatLookup cheats{};

    std::span<const svc::DiskFormatRow> disk_formats{};

    std::span<const OptionRow> option_rows{};

    std::span<const std::string_view> option_pages{};

    std::span<const CoreWindowDecl> windows{};

    bool file_tx_whole = false;

    ResetRow reset{};

    StartMount undeclared_start_mount = StartMount::Generic;

    SlotRole undeclared_role = SlotRole::Image;

    HostPolicy host{};

    infra::OptRef<const svc::ISectorProgram> sector_program{};
};

[[nodiscard]] constexpr StartMount start_mount_of(const CoreProfile& p, IoIndex index) {
    return start_mount_of(p.slots, index, p.undeclared_start_mount);
}

[[nodiscard]] constexpr SlotRole role_of(const CoreProfile& p, IoIndex index) {
    return role_of(p.slots, index, p.undeclared_role);
}

[[nodiscard]] Ex<void> assert_services_match_census(
    const CoreProfile& p,
    std::span<const LinkDecoderDecl> census_rows
);

}
