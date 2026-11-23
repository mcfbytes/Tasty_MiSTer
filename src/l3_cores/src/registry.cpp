// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/registry.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "cores/core_window_grant.h"
#include "cores/generic_core.h"
#include "cores/manifests/atari8.h"
#include "cores/manifests/megadrive.h"
#include "cores/manifests/minimig.h"
#include "cores/minimig/a2065_nic.h"
#include "cores/manifests/menu.h"
#include "cores/manifests/psx.h"
#include "cores/manifests/snes.h"
#include "cores/mra/arcade_core.h"
#include "cores/mra/arcade_manifest_role.h"
#include "cores/manifests/cdi.h"
#include "cores/manifests/mac.h"
#include "cores/manifests/electron.h"
#include "cores/manifests/megacd.h"
#include "cores/manifests/n64.h"
#include "cores/manifests/neogeo.h"
#include "cores/manifests/pce.h"
#include "cores/manifests/saturn.h"
#include "cores/manifests/st.h"
#include "cores/manifests/threedo.h"
#include "cores/manifests/a2.h"

#include "cores/manifests/c128.h"
#include "cores/manifests/uneon.h"
#include "cores/manifests/x86.h"
#include "cores/manifests/archie.h"
#include "cores/archie/kart_controller.h"
#include "cores/minimig/amiga_keyboard.h"
#include "cores/input_sink.h"

#include "cores/manifests/c64.h"

#include "cores/manifests/zx81.h"
#include "cores/manifests/amstrad.h"
#include "hal/board_profile.h"
#include "infra/ascii.h"
#include "proto/disk_protocol_row.h"
#include "infra/indexed_array.h"

namespace mister::cores {
namespace {

constexpr auto kCoreTable = std::to_array<CoreFactory>({
    {.profile = manifests::kMenu, .make = &make_generic},
    {.profile = manifests::kMegaCd,
     .make = &manifests::make_megacd,
     .make_ladder = &manifests::make_megacd_ladder},
    {.profile = manifests::kPceCd,
     .make = &manifests::make_pcecd,
     .make_ladder = &manifests::make_pcecd_ladder},
    {.profile = manifests::kPsx,
     .make = &manifests::make_psx,
     .make_ladder = &manifests::make_psx_ladder},
    {.profile = manifests::kSnes,
     .make = &manifests::make_snes,
     .make_loader = &manifests::make_snes_loader,
     .derives_content = true,
     .make_companion = &manifests::make_msu_companion,
     .mailbox_servant = manifests::kMsuServant},
    {.profile = manifests::kSgb,
     .make = &manifests::make_sgb,
     .make_companion = &manifests::make_msu_companion,
     .mailbox_servant = manifests::kMsuServant},
    {.profile = manifests::kElectron,
     .make = &manifests::make_electron,
     .make_loader = &manifests::make_uef_loader,
     .derives_content = true},
    {.profile = manifests::kAtariSt,
     .make = &manifests::make_st,
     .make_ladder = &manifests::make_st_ladder,
     .make_loader = &manifests::make_st_loader,
     .disk_servants = manifests::kAtariStDiskServants},
    {.profile = manifests::kThreeDo,
     .make = &manifests::make_threedo,
     .make_ladder = &manifests::make_threedo_ladder},
    {.profile = manifests::kMegaDrive, .make = &manifests::make_megadrive},
    {.profile = manifests::kN64,
     .make = &manifests::make_n64,
     .make_loader = &manifests::make_n64_loader,
     .make_save_store = &manifests::make_n64_save_store},
    {.profile = manifests::kNeoGeo,
     .make = &manifests::make_neogeo,
     .make_ladder = &manifests::make_neogeo_ladder,
     .make_loader = &manifests::make_neogeo_loader},
    {.profile = manifests::kAo486,
     .make = &manifests::make_ao486,
     .make_ladder = &manifests::make_ao486_ladder,
     .make_loader = &manifests::make_ao486_loader,
     .disk_servants = manifests::kAo486DiskServants,
     .make_share_host = &manifests::make_ao486_share_host},
    {.profile = manifests::kPc110, .make = &make_generic},
    {.profile = manifests::kZ486, .make = &make_generic},
    {.profile = manifests::kZ386, .make = &make_generic},
    {.profile = manifests::kPcxt,
     .make = &manifests::make_pcxt,
     .make_ladder = &manifests::make_pcxt_ladder,
     .disk_servants = manifests::kPcxtDiskServants},
    {.profile = manifests::kTandy1000,
     .make = &manifests::make_pcxt,
     .make_ladder = &manifests::make_pcxt_ladder,
     .disk_servants = manifests::kPcxtDiskServants},
    {.profile = manifests::kPcjr,
     .make = &manifests::make_pcjr,
     .make_ladder = &manifests::make_pcjr_ladder,
     .disk_servants = manifests::kPcjrDiskServants},
    {.profile = manifests::kPcxtEga,
     .make = &manifests::make_pcxt,
     .make_ladder = &manifests::make_pcxt_ladder,
     .disk_servants = manifests::kPcxtDiskServants},
    {.profile = manifests::kUneon, .make = &make_generic},
    {.profile = manifests::kSaturn,
     .make = &manifests::make_saturn,
     .make_ladder = &manifests::make_saturn_ladder},
    {.profile = manifests::kCdi,
     .make = &manifests::make_cdi,
     .make_ladder = &manifests::make_cdi_ladder},
    {.profile = manifests::kArchie,
     .make = &manifests::make_archie,
     .make_ladder = &manifests::make_archie_ladder},
    {.profile = manifests::kTk2000, .make = &make_generic},
    {.profile = manifests::kOric, .make = &make_generic},
    {.profile = manifests::kMacLc,
     .make = &manifests::make_maclc,
     .slot_servants = manifests::kMacLcSlotServants},
    {.profile = manifests::kZx81,
     .make = &make_generic,
     .make_sidecar = &manifests::make_zx81_sidecars},
    {.profile = manifests::kAmstrad, .make = &make_generic},
    {.profile = manifests::kC128, .make = &make_generic},

    {.profile = mra::kArcadeProfile,
     .make = &mra::make_arcade,
     .manifest_doc = infra::OptRef<const IManifestDocRole>{mra::kMraRole}},
    {.profile = manifests::kAtari800,
     .make = &manifests::make_atari800,
     .make_loader = &manifests::make_atari800_loader,
     .mailbox_servant = manifests::kAtari800Servant},
    {.profile = manifests::kAtari5200,
     .make = &manifests::make_atari5200,
     .make_loader = &manifests::make_atari5200_loader,
     .mailbox_servant = manifests::kAtari5200Servant},
    {.profile = manifests::kMinimig,
     .make = &manifests::make_minimig,
     .make_ladder = &manifests::make_minimig_ladder,
     .disk_servants = manifests::kMinimigDiskServants,
     .mailbox_servant = manifests::kMinimigCdServant,
     .make_nic = &minimig::make_a2065_nic,
     .make_share_host = &manifests::make_minimig_share_host},
    {.profile = manifests::kC64,
     .make = &manifests::make_c64,
     .make_loader = &manifests::make_c64_loader},
    {.profile = manifests::kAppleII, .make = &make_generic},
    {.profile = manifests::kAppleIIgs, .make = &make_generic},
});

consteval bool channel_rows_carry_a_store() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.saves_by_channel != f.saves_by_channel())
            return false;
    return true;
}

static_assert(
    channel_rows_carry_a_store(),
    "a saves_by_channel row needs make_save_store, and only such a row has one"
);

consteval bool nic_rows_have_a_ladder() {
    for (const CoreFactory& f : kCoreTable)
        if (f.has_nic() && !f.has_ladder())
            return false;
    return true;
}

static_assert(nic_rows_have_a_ladder(), "a make_nic row needs make_ladder: the ladder posts Start");

consteval bool disk_rows_name_their_servants() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile.grants.has(Grant::Disk) != f.serves_disks())
            return false;
        if (f.serves_disks() && f.profile.disk_formats.empty())
            return false;
        for (std::size_t i = 0; i < f.disk_servants.size(); ++i) {
            const DiskServantRow& r = f.disk_servants[i];
            if (r.protocol == proto::DiskProtocol::None || r.make == nullptr)
                return false;

            if (proto::disk_protocol_row(r.protocol).protocol != r.protocol)
                return false;
            for (std::size_t j = i + 1; j < f.disk_servants.size(); ++j)
                if (f.disk_servants[j].protocol == r.protocol)
                    return false;
        }
    }
    return true;
}

static_assert(
    disk_rows_name_their_servants(),
    "a row grants Disk exactly when it declares disk servants: each names a protocol "
    "of the protocol table once with its factory, and the row declares the disk formats "
    "the servant opens"
);

consteval bool window_rows_are_joined() {
    for (const CoreFactory& f : kCoreTable) {
        const CoreProfile& p = f.profile;
        if (!lba_windows_joined(p.lba_windows))
            return false;
        const auto shaped = [&p](proto::SlotIndex slot, WindowShape shape) {
            for (const LbaWindowRow& w : p.lba_windows)
                if (w.slot == slot && w.shape == shape)
                    return true;
            return false;
        };
        const auto served = [&f](proto::SlotIndex slot) {
            std::size_t rows = 0;
            for (const SlotServantRow& s : f.slot_servants)
                if (s.slot == slot)
                    ++rows;
            return rows;
        };
        for (const LbaWindowRow& w : p.lba_windows)
            if (served(w.slot) != 1)
                return false;
        for (const SlotServantRow& s : f.slot_servants) {
            if (s.make == nullptr || s.slot > proto::kLastAnnounceableSlot)
                return false;
            bool windowed = false;
            for (const LbaWindowRow& w : p.lba_windows)
                windowed = windowed || w.slot == s.slot;
            if (!windowed)
                return false;
        }
        for (std::size_t i = 0; i < p.control_slots.size(); ++i) {
            const ControlSlot& c = p.control_slots[i];
            if (c.subdir.empty() || !shaped(c.slot, WindowShape::Rpc))
                return false;
            for (std::size_t j = i + 1; j < p.control_slots.size(); ++j)
                if (p.control_slots[j].slot == c.slot)
                    return false;
        }
        if (p.repulse.declared() && !shaped(p.repulse.slot, WindowShape::Data))
            return false;
        if (f.serves_slots() != p.grants.has(Grant::Servants))
            return false;
        if (!f.serves_slots() && (!p.control_slots.empty() || p.repulse.declared()))
            return false;
    }
    return true;
}

static_assert(
    window_rows_are_joined(),
    "a row's LBA windows ascend and share no storage byte; a windowed slot has one "
    "servant row and a servant row a window; a control slot carries an Rpc window and "
    "a repulse rule a Data one; the Servants grant goes with exactly these rows"
);

consteval bool mirror_rows_are_joined() {
    for (const CoreFactory& f : kCoreTable) {
        const CoreProfile& p = f.profile;
        for (std::size_t i = 0; i < p.slots.size(); ++i) {
            const FileSlot& s = p.slots[i];
            if (!s.mirror)
                continue;
            if (!s.mirror->fits(p.windows))
                return false;
            if (disk_formats_of(p, s.index).empty())
                return false;
            if (!slot_service_serves(s.role) && !s.via_slot_service)
                return false;
            for (std::size_t j = i + 1; j < p.slots.size(); ++j) {
                const FileSlot& other = p.slots[j];
                if (other.mirror && s.mirror->overlaps(*other.mirror, p.windows))
                    return false;
            }
        }
    }
    return true;
}

static_assert(
    mirror_rows_are_joined(),
    "a slot's mirror names a declared window and lies inside it, a profile's mirrors "
    "do not alias, the slot's role reaches the save mount, and it opens under format "
    "rows"
);

consteval bool a_row_speaks_one_disk_protocol() {
    for (const CoreFactory& f : kCoreTable)
        if (f.disk_servants.size() > 1)
            return false;
    return true;
}

static_assert(
    a_row_speaks_one_disk_protocol(),
    "a row that declares two disk servants lets two relays write an act in one round: "
    "the disk relays need one round rule between them before that row lands"
);

consteval bool in_servant_set(ServantId id) {
    const infra::IndexedArray<bool, ServantId::kCount, ServantId> ids{};
    return ids.find(id).has_value();
}

consteval bool mailbox_rows_name_their_servants() {
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        const CoreFactory& f = kCoreTable[i];
        const MailboxServantRow& s = f.mailbox_servant;
        if (f.profile.grants.has(Grant::Mailbox) != s.declared())
            return false;
        if (s.declared() == (s.id == ServantId::None) || !in_servant_set(s.id))
            return false;
        if (f.make_companion != nullptr && !s.declared())
            return false;
        for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
            const MailboxServantRow& o = kCoreTable[j].mailbox_servant;
            if (s.declared() && o.id == s.id && o.make != s.make)
                return false;
        }
    }
    return true;
}

static_assert(
    mailbox_rows_name_their_servants(),
    "a row grants Mailbox exactly when it declares a mailbox servant under an id of "
    "the set; a companion's row declares one; one id has one factory"
);

consteval bool kinds_are_the_rows() {
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        if (kCoreTable[i].profile.kind == CoreKind::Generic)
            return false;
        for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
            if (kCoreTable[i].profile.kind == kCoreTable[j].profile.kind)
                return false;
        }
    }
    return kCoreTable.size() + 1 == std::to_underlying(CoreKind::Count);
}

static_assert(
    kinds_are_the_rows(),
    "CoreKind must be exactly the registry rows plus Generic: an enumerator "
    "with no row is a name nothing can select, and a row sharing a kind is "
    "two factories for one discriminant"
);

consteval std::size_t tasty_rows() {
    std::size_t n = 0;
    for (const CoreFactory& f : kCoreTable)
        n += f.profile.tasty_playable ? 1u : 0u;
    return n;
}

static_assert(tasty_rows() == 4, "exactly four registry rows are tasty_playable");

consteval bool tasty_names_join() {
    constexpr std::string_view kTastyNames[]{"MENU", "SNES", "MegaDrive", "PSX"};
    if (tasty_rows() != std::size(kTastyNames))
        return false;
    for (std::string_view want : kTastyNames) {
        std::size_t tagged = 0;
        for (const CoreFactory& f : kCoreTable)
            tagged += (f.profile.tasty_playable && f.profile.name == want) ? 1u : 0u;
        if (tagged != 1)
            return false;
    }
    return true;
}

static_assert(
    tasty_names_join(),
    "the tasty rows are MENU, SNES, MegaDrive and PSX by name: `tasty_plays` "
    "answers a CONF_STR name, so a tag on a renamed row is a core tasty cannot load"
);

constexpr bool takes_the_rowless_route(const CoreProfile& p) {
    return p.undeclared_role == kGenericProfile.undeclared_role
        && p.undeclared_start_mount == kGenericProfile.undeclared_start_mount;
}

consteval bool generic_rows_start_no_mount() {
    for (const CoreFactory& f : kCoreTable)
        if (f.make == &make_generic
            && !takes_the_rowless_route(f.profile)
            && f.profile.undeclared_start_mount != StartMount::Unmodelled)
            return false;
    return true;
}

static_assert(
    generic_rows_start_no_mount(),
    "a make_generic row arms an SC start mount only with the whole rowless route"
);

consteval bool generic_rows_keep_the_class_route() {
    for (const CoreFactory& f : kCoreTable)
        if (f.make == &make_generic
            && !takes_the_rowless_route(f.profile)
            && f.profile.undeclared_role != SlotRole::Image)
            return false;
    return true;
}

static_assert(
    generic_rows_keep_the_class_route(),
    "a make_generic row routes an undeclared slot to the slot service only with the "
    "whole rowless route"
);

consteval bool slot_formats_ride_the_slot_service() {
    for (const CoreFactory& f : kCoreTable) {
        for (const FileSlot& s : f.profile.slots) {
            if (s.disk_formats.empty())
                continue;
            if (s.role != SlotRole::Drive || is_load_row(s))
                return false;
            const svc::DiskFormatRow& last = s.disk_formats.back();
            if ((last.codec != svc::DiskCodec::Raw && !svc::admits_by_content(last.codec))
                || !last.ext.empty()
                || !last.magic.empty()
                || last.size_bytes != 0)
                return false;

            for (const svc::DiskFormatRow& row : s.disk_formats.first(s.disk_formats.size() - 1))
                if (row.ext.empty())
                    return false;
        }
    }
    return true;
}

static_assert(
    slot_formats_ride_the_slot_service(),
    "a FileSlot's disk_formats needs a Drive mount row, an extension on every row but "
    "the last, and a last catch-all row, Raw or a codec that admits by content"
);

consteval bool rowless_profile_keeps_its_defaults() {
    return kGenericProfile.kind == CoreKind::Generic
        && kGenericProfile.name.empty()
        && kGenericProfile.undeclared_role == SlotRole::Save
        && kGenericProfile.undeclared_start_mount == StartMount::Generic
        && kGenericProfile.start == StartRule::Chain;
}

static_assert(
    rowless_profile_keeps_its_defaults(),
    "kGenericProfile must stay Generic/nameless/Save/Generic/Chain: it is the answer "
    "every rowless core is built over"
);

consteval bool no_row_defaults_to_a_disc() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.undeclared_role == SlotRole::Disc)
            return false;
    return true;
}

static_assert(no_row_defaults_to_a_disc(), "no row's undeclared_role may be Disc");

consteval bool caps_slots_route_alike_under_a_manifest() {
    for (const CoreFactory& f : kCoreTable) {
        for (const FileSlot& s : f.profile.slots) {
            if (is_load_row(s) || s.mount_index_form != MountIndexForm::ImageCaps)
                continue;
            if (role_of(f.profile, s.index) != role_of(mra::kArcadeProfile, s.index))
                return false;
            if (start_mount_of(f.profile, s.index) != start_mount_of(mra::kArcadeProfile, s.index))
                return false;
        }
    }
    return true;
}

static_assert(
    caps_slots_route_alike_under_a_manifest(),
    "an ImageCaps slot takes the same route under its own row and under the manifest "
    "row: MAIN reads the index form off the name and the route off the build"
);

consteval bool block_fact_rows_are_generic(const CoreProfile& p) {
    return !p.host.front_end
        && p.services.empty()
        && p.slots.empty()
        && p.boot_assets.empty()
        && p.start_assets.empty()
        && p.signatures.empty()
        && p.windows.empty()
        && !p.staging.staged
        && !p.suppresses_mgl
        && !p.suppress_reset_status_write
        && !p.host.suppress_analog_followup
        && !p.host.analog_reshape
        && p.host.cue_browse_dir.empty()
        && p.default_uart_baud == kDefaultUartBaud
        && p.disk_formats.empty()
        && p.option_rows.empty()
        && p.option_pages.empty()
        && p.init_additions.empty()
        && !p.blank_save.declared();
}

static_assert(
    block_fact_rows_are_generic(manifests::kPc110)
    && block_fact_rows_are_generic(manifests::kZ486)
    && block_fact_rows_are_generic(manifests::kZ386)
    && block_fact_rows_are_generic(manifests::kUneon)
);

consteval bool owns_its_block_service(const CoreProfile& p) {
    return p.block_service == BlockService::CoreOwn;
}

static_assert(
    owns_its_block_service(manifests::kAo486)
    && owns_its_block_service(manifests::kPc110)
    && owns_its_block_service(manifests::kZ486)
    && owns_its_block_service(manifests::kZ386)
    && owns_its_block_service(manifests::kPcxt)
    && owns_its_block_service(manifests::kTandy1000)
    && owns_its_block_service(manifests::kPcjr)
    && owns_its_block_service(manifests::kPcxtEga)
);

static_assert(
    manifests::kAo486.host.pause == PauseOnOsd::Option,
    "a row that declares no host policy must take the Option default"
);
static_assert(
    manifests::kUneon.block_service == BlockService::Generic
    && manifests::kUneon.block_drain_budget == 3
);

consteval bool starts_on_its_own_arm(const CoreProfile& p) {
    return p.start == StartRule::None;
}

static_assert(
    starts_on_its_own_arm(manifests::kAo486)
    && starts_on_its_own_arm(manifests::kPc110)
    && starts_on_its_own_arm(manifests::kZ486)
    && starts_on_its_own_arm(manifests::kZ386)
    && starts_on_its_own_arm(manifests::kPcxt)
    && starts_on_its_own_arm(manifests::kTandy1000)
    && starts_on_its_own_arm(manifests::kPcjr)
    && starts_on_its_own_arm(manifests::kPcxtEga)
);
static_assert(manifests::kUneon.start == StartRule::Chain);
static_assert(manifests::kCdi.block_drain_budget == 1);
static_assert(manifests::kPsx.block_drain_budget == 1);

static_assert(
    manifests::kPsx.block_sector_floor.is_sector(2352)
    && !manifests::kPsx.block_sector_floor.is_sector(1024)
);
static_assert(manifests::kCdi.block_sector_floor.is_sector(2568));

consteval bool host_bit_rows_are_well_formed() {
    for (const CoreFactory& f : kCoreTable) {
        const std::span<const HostBitRow> rows = f.profile.host_bits;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!rows[i].bits.valid() || !rows[i].bits.fits_status())
                return false;
            for (std::size_t j = i + 1; j < rows.size(); ++j) {
                if (rows[i].fact == rows[j].fact)
                    return false;
                const proto::BitField& a = rows[i].bits;
                const proto::BitField& b = rows[j].bits;
                const std::optional<proto::StatusBit> a_last = a.end();
                const std::optional<proto::StatusBit> b_last = b.end();
                if (!a_last || !b_last)
                    return false;
                if (a.start <= *b_last && b.start <= *a_last)
                    return false;
            }
        }
    }
    return true;
}

static_assert(
    host_bit_rows_are_well_formed(),
    "a host_bits row is a valid field inside the status word, its fact declared once "
    "per profile, overlapping no other row"
);

consteval bool unless_rows_are_read() {
    const auto declares = [](const CoreProfile& p, HostFact fact) consteval {
        for (const HostBitRow& r : p.host_bits)
            if (r.fact == fact)
                return true;
        return false;
    };
    for (const CoreFactory& f : kCoreTable) {
        for (const BootAsset& r : f.profile.boot_assets)
            if (r.unless)
                return false;
        for (const BootAsset& r : f.profile.start_assets)
            if (r.unless && (f.has_ladder() || !declares(f.profile, *r.unless)))
                return false;
    }
    return true;
}

static_assert(
    unless_rows_are_read(),
    "a BootAsset::unless sits on a start_assets row of a ladderless profile, and names "
    "a HostFact that profile's host_bits declares"
);

consteval bool staging_cores_declare_blank_save() {
    for (const CoreFactory& f : kCoreTable) {
        if (!f.profile.staging.staged)
            continue;
        if (!f.profile.blank_save.declared() && !f.profile.blank_save.generic_ff)
            return false;
    }
    return true;
}

static_assert(
    staging_cores_declare_blank_save(),
    "a CoreProfile with a staging ladder mounts a deferred-create "
    "save slot, so it MUST declare blank_save: without it every "
    "read of a fresh save is answered 0xFF and the core reads its "
    "backup RAM as corrupt (pass BS / row item)"
);

consteval bool blank_fills_are_the_two_typed_bytes() {

    constexpr std::byte kZero{};
    for (const CoreFactory& f : kCoreTable) {
        const std::byte fill = f.profile.blank_save.fill;
        if (fill != kZero && fill != svc::BlankSaveSpec::kErased)
            return false;
    }
    return true;
}

static_assert(
    blank_fills_are_the_two_typed_bytes(),
    "a profile's blank_save.fill is std::byte{0x00} or BlankSaveSpec::kErased"
);

consteval std::size_t boot_buffers_of(std::span<const BootAsset> rows) {
    std::size_t outside = 0;
    bool group0 = false;
    for (const BootAsset& r : rows) {
        group0 = group0 || r.group == BootGroup::First;
        outside += r.group == BootGroup::First ? 0u : 1u;
    }
    return outside + (group0 ? 1u : 0u);
}

consteval BootAsset row_in(BootGroup group) {
    return BootAsset{.name = {}, .where = {}, .group = group};
}

constexpr BootAsset kTwoLayeredRows[] = {
    row_in(BootGroup::First),
    row_in(BootGroup::First),
    row_in(BootGroup::Second),
    row_in(BootGroup::Second)
};
static_assert(boot_buffers_of(kTwoLayeredRows) == 3);

consteval bool boot_buffers_fit_the_ceiling() {
    for (const CoreFactory& f : kCoreTable) {
        if (boot_buffers_of(f.profile.boot_assets) > kMaxBootBuffers)
            return false;
    }
    return true;
}

static_assert(
    boot_buffers_fit_the_ceiling(),
    "a manifest whose ladder can load more than kMaxBootBuffers boot assets "
    "outgrows app::InternedFile::kMaxLiveFiles; raise it only with its derivation"
);

consteval bool start_assets_fit_the_ceiling() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile.start_assets.size() > kMaxStartAssets)
            return false;
    }
    return true;
}

static_assert(
    start_assets_fit_the_ceiling(),
    "a manifest with more than kMaxStartAssets start assets outgrows "
    "app::InternedFile::kMaxLiveFiles; raise it only with its derivation"
);

constexpr bool declares_its_chain(const CoreProfile& p) {
    return p.start == StartRule::Chain && !p.start_assets.empty();
}

consteval bool start_rule_matches_its_rows() {
    for (const CoreFactory& f : kCoreTable)
        if (!declares_its_chain(f.profile)
            && (f.profile.start == StartRule::Assets) == f.profile.start_assets.empty())
            return false;
    return true;
}

static_assert(
    start_rule_matches_its_rows(),
    "StartRule::Assets is the rule of exactly the rows that declare start_assets, "
    "a Chain row's own chain aside"
);

consteval bool no_manifest_row_anchors_core_home() {
    const auto anchored = [](std::span<const BootAsset> rows) consteval {
        for (const BootAsset& r : rows)
            if (r.anchor == AssetAnchor::CoreHome)
                return true;
        return false;
    };
    for (const CoreFactory& f : kCoreTable)
        if (anchored(f.profile.boot_assets)
            || (!declares_its_chain(f.profile) && anchored(f.profile.start_assets)))
            return false;
    return true;
}

static_assert(
    no_manifest_row_anchors_core_home(),
    "AssetAnchor::CoreHome is the start chain's alone; no ladder can resolve it"
);

consteval bool chain_rows_are_well_formed() {
    const auto plain = [](std::span<const BootAsset> rows) consteval {
        for (const BootAsset& r : rows)
            if (r.sweep != AssetSweep::None || !r.dropped_by_exts.empty())
                return false;
        return true;
    };
    for (const CoreFactory& f : kCoreTable) {
        if (!plain(f.profile.boot_assets))
            return false;
        if (!declares_its_chain(f.profile)) {
            if (!plain(f.profile.start_assets))
                return false;
            continue;
        }
        if (f.has_ladder())
            return false;
        for (const BootAsset& r : f.profile.start_assets) {
            if (r.anchor != AssetAnchor::CoreHome || !r.subdir.empty())
                return false;

            if (r.sweep != AssetSweep::None && r.dest == WideIoIndex{})
                return false;
            if (!r.dropped_by_exts.empty() && r.dest != WideIoIndex{})
                return false;
        }
    }
    return true;
}

static_assert(
    chain_rows_are_well_formed(),
    "a Chain row's own rows anchor at the core home with no subdir; a sweep is never "
    "at index 0, a replaceable row always is, and no other row carries either fact"
);

consteval bool front_end_and_hint_rows_start_nothing() {
    for (const CoreFactory& f : kCoreTable)
        if ((f.profile.host.front_end || f.profile.name.empty())
            && f.profile.start != StartRule::None)
            return false;
    return true;
}

static_assert(
    front_end_and_hint_rows_start_nothing(),
    "the front end and a hint-only row run their own start arm: StartRule::None"
);

consteval bool front_end_is_the_menu_kind() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.host.front_end != (f.profile.kind == CoreKind::Menu))
            return false;
    return !kGenericProfile.host.front_end;
}

static_assert(front_end_is_the_menu_kind(), "HostPolicy::front_end is set on the Menu row alone");

consteval bool start_ladder_rows_take_assets() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.staging.save
            && !f.profile.staging.save->start_dir.empty()
            && f.profile.start != StartRule::Assets)
            return false;
    return true;
}

static_assert(
    start_ladder_rows_take_assets(),
    "a start ladder carries declared rows only: a start-dir save takes Assets"
);

consteval bool manifest_windows_fit_every_sourced_aperture() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile.windows.size() > kMaxCoreWindows)
            return false;
        for (const hal::FpgaAperture& ap : hal::kBoardApertures) {
            if (ap.region.len == 0)
                continue;
            std::array<hal::FabricRegion, kMaxCoreWindows> rows{};
            std::size_t n = 0;
            for (const CoreWindowDecl& w : f.profile.windows)
                rows[n++] = w.region;
            if (!hal::fabric_rows_well_formed(std::span(rows).first(n), ap))
                return false;
        }
        std::size_t feeders = 0;
        for (const CoreWindowDecl& w : f.profile.windows) {
            if (w.region.name == nullptr || *w.region.name == '\0')
                return false;
            feeders += w.feeds_pcm ? 1u : 0u;
        }
        if (feeders > 1)
            return false;
    }
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        for (const CoreWindowDecl& a : kCoreTable[i].profile.windows) {
            for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
                for (const CoreWindowDecl& b : kCoreTable[j].profile.windows) {
                    if (std::string_view(a.region.name) == std::string_view(b.region.name)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static_assert(
    manifest_windows_fit_every_sourced_aperture(),
    "a declared core window must fit inside every sourced board aperture, carry "
    "a name, share that name with no other manifest's row, number at most "
    "kMaxCoreWindows per manifest and feed PCM from at most one row"
);

consteval bool share_rows_are_joined() {
    for (const CoreFactory& f : kCoreTable) {
        const bool declared = f.profile.share.has_value();
        if (declared != (f.make_share_host != nullptr))
            return false;
        if (!declared)
            continue;
        const ShareRow& row = *f.profile.share;
        if (row.bytes.empty() || row.default_dir.empty())
            return false;
        if (!row.page.holds(f.profile.windows, row.bytes))
            return false;
        if (row.poll <= infra::kNoWait)
            return false;
    }
    return true;
}

static_assert(
    share_rows_are_joined(),
    "a row declares a ShareRow exactly when it names a share host factory, and the "
    "row's page and its length lie inside the window its spot names"
);

consteval bool staging_rows_declare_a_ladder() {
    for (const CoreFactory& f : kCoreTable) {
        const bool staged = f.profile.staging.staged;
        const bool laddered = staged || ladder_rows(f.profile.slots) != 0;
        if (laddered != f.has_ladder())
            return false;
        if (f.profile.reset.ladders != (!staged && laddered))
            return false;
    }
    return true;
}

static_assert(
    staging_rows_declare_a_ladder(),
    "a CoreProfile with a staging order or a Ladder slot names the ladder that runs "
    "beside it, a profile with neither names none, and only a Ladder-slot row resets "
    "through it"
);

consteval bool host_served_drives_own_the_block_service() {
    for (const CoreFactory& f : kCoreTable) {
        for (const DiskServantRow& d : f.disk_servants) {
            for (const proto::DiskProtocolRow& row : proto::kDiskProtocols) {
                if (row.protocol != d.protocol)
                    continue;
                for (const FileSlot& s : f.profile.slots) {
                    if (s.role != SlotRole::Ladder)
                        continue;
                    if (!row.units.contains(s.index.served_unit()))
                        continue;
                    if (f.profile.block_service != BlockService::CoreOwn)
                        return false;
                }
            }
        }
    }
    return true;
}

static_assert(
    host_served_drives_own_the_block_service(),
    "a Ladder slot at a disk-servant unit's index is a host-served drive, legal only "
    "on a BlockService::CoreOwn row: no generic drain may serve that index too"
);

consteval bool reinit_rows_ladder() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.reset.reinits && (!f.profile.reset.ladders || !f.has_ladder()))
            return false;
    return true;
}

static_assert(reinit_rows_ladder(), "ResetRow::reinits implies ladders and a make_ladder column");

consteval bool laddered_rows_declare_one_disc_slot() {
    for (const CoreFactory& f : kCoreTable) {
        const std::size_t discs = disc_rows(f.profile.slots);
        const bool staged = f.profile.staging.staged;
        if (discs != (staged ? 1u : 0u))
            return false;
        if (discs == 1 && disc_slot_of(f.profile.slots) != f.profile.staging.disc_slot)
            return false;
    }
    return true;
}

static_assert(
    laddered_rows_declare_one_disc_slot(),
    "a staged row declares exactly one SlotRole::Disc slot and derives "
    "staging.disc_slot from it; any other row declares no Disc slot"
);

consteval bool staged_rows_grant_the_disc_crossing() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile.staging.staged && !f.profile.grants.has(Grant::Discs))
            return false;
    return true;
}

static_assert(
    staged_rows_grant_the_disc_crossing(),
    "a staged row's core reads HostServices::discs, so its profile must grant Discs"
);

consteval bool loader_rows_stream() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.has_loader() && !f.streams_files())
            return false;
    }
    return true;
}

static_assert(
    loader_rows_stream(),
    "a row with a MAIN loader answers the stream role: its profile cannot say no_stream_role"
);

consteval bool derived_rows_have_a_loader() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.derives_content && !f.has_loader())
            return false;
    }
    return true;
}

static_assert(derived_rows_have_a_loader(), "derives_content implies make_loader");

consteval bool loader_rows_keep_their_loader() {
    for (const CoreFactory& f : kCoreTable) {
        if (!f.has_loader())
            continue;
        const infra::OptRef<const CoreFactory> row{f};
        if (loader_for(row, hal::FabricAddr{}) != f.make_loader
            || loader_for(row, hal::FabricAddr{1}) != f.make_loader)
            return false;
    }
    return true;
}

static_assert(
    loader_rows_keep_their_loader(),
    "loader_for answers a row's own loader whatever the pick's address"
);

consteval bool companion_rows_declare_a_window() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.make_companion != nullptr && f.profile.windows.empty())
            return false;
    }
    return true;
}

static_assert(
    companion_rows_declare_a_window(),
    "a row with a MAIN companion declares the window its sibling file loads into"
);

consteval proto::FileSize largest_save_of_the_table() {
    proto::FileSize most{};
    for (const CoreFactory& f : kCoreTable)
        most = std::max(most, f.profile.largest_save);
    return most;
}

constexpr proto::FileSize kLargestDeclaredSave = largest_save_of_the_table();
static_assert(
    kLargestDeclaredSave == proto::FileSize{1'050'688},
    "the save lane holds an EasyFlash image whole, 64 + 128 x 8208 bytes "
    "(support/c64/c64.cpp ezfl_size): 2,112 bytes over the manifest saves' 1 MiB"
);

consteval bool sidecar_rows_are_alone() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.make_sidecar == nullptr)
            continue;
        if (f.make_companion != nullptr || !f.streams_files())
            return false;
    }
    return true;
}

static_assert(
    sidecar_rows_are_alone(),
    "a row with a MAIN sidecar builder has no companion and answers the stream role"
);

consteval bool serviced_rows_have_a_class_half() {
    for (const CoreFactory& f : kCoreTable)
        for (const FileSlot& s : f.profile.slots)
            if (s.via_slot_service
                && (s.role != SlotRole::Image || is_load_row(s) || f.make == &make_generic))
                return false;
    return true;
}

static_assert(
    serviced_rows_have_a_class_half(),
    "FileSlot::via_slot_service names an Image row of a core class that answers "
    "on_mount: a Save row is the slot service's already, and GenericCore refuses it"
);

}

std::span<const CoreFactory> core_table() {
    return kCoreTable;
}

proto::FileSize largest_declared_save() noexcept {
    return kLargestDeclaredSave;
}

[[nodiscard]] infra::OptRef<const CoreFactory> find_core(std::string_view conf_str_name) {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile.name.empty())
            continue;
        if (infra::ieq(f.profile.name, conf_str_name))
            return infra::OptRef<const CoreFactory>{f};
    }
    return {};
}

[[nodiscard]] infra::OptRef<const CoreFactory> find_core(
    std::string_view conf_str_name,
    LoadHint hint
) {
    if (hint == LoadHint::XmlManifest) {
        for (const CoreFactory& f : kCoreTable) {
            if (f.manifest_doc)
                return infra::OptRef<const CoreFactory>{f};
        }
        return {};
    }
    return find_core(conf_str_name);
}

infra::OptRef<const IManifestDocRole> manifest_doc_role() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.manifest_doc)
            return f.manifest_doc;
    }
    return {};
}

std::unique_ptr<IInputSink> make_input_sink(InputSinkId id) {
    switch (id) {
        case InputSinkId::KbdController:
            return std::make_unique<archie::KartController>();
        case InputSinkId::PacedKeycodes:
            return std::make_unique<minimig::AmigaKeyboard>();
        case InputSinkId::None:
        case InputSinkId::kCount:
            break;
    }
    return nullptr;
}

const CoreProfile& profile_for(std::string_view conf_str_name) {
    const auto row = find_core(conf_str_name);
    return row ? row->profile : kGenericProfile;
}

const CoreProfile& profile_for(std::string_view conf_str_name, LoadHint hint) {
    const auto row = find_core(conf_str_name, hint);
    return row ? row->profile : kGenericProfile;
}

}
