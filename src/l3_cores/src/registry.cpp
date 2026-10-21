// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/registry.h"

#include <array>
#include <cctype>
#include <span>
#include <string_view>
#include <utility>

#include "cores/core_window_grant.h"
#include "cores/generic_core.h"
#include "cores/manifests/megadrive.h"
#include "cores/manifests/psx.h"
#include "cores/manifests/snes.h"
#include "cores/arcade_core.h"
#include "cores/arcade_manifest_role.h"
#include "cores/manifests/cdi.h"
#include "cores/manifests/electron.h"
#include "cores/manifests/megacd.h"
#include "cores/manifests/n64.h"
#include "cores/manifests/neogeo.h"
#include "cores/manifests/pce.h"
#include "cores/manifests/saturn.h"
#include "cores/manifests/st.h"
#include "cores/manifests/threedo.h"
#include "cores/manifests/uneon.h"
#include "cores/manifests/x86.h"
#include "hal/board_profile.h"

namespace mister::cores {
namespace {

constexpr auto kCoreTable = std::to_array<CoreFactory>({
    {.kind = CoreKind::Menu, .name = "MENU", .profile = &kMenuProfile, .make = &make_generic},
    {.kind = CoreKind::MegaCd,
     .name = "MEGACD",
     .profile = &manifests::kMegaCd,
     .make = &manifests::make_megacd,
     .make_ladder = &manifests::make_megacd_ladder},
    {.kind = CoreKind::PceCd,
     .name = "TGFX16",
     .profile = &manifests::kPceCd,
     .make = &manifests::make_pcecd,
     .make_ladder = &manifests::make_pcecd_ladder},
    {.kind = CoreKind::Psx,
     .name = "PSX",
     .profile = &manifests::kPsx,
     .make = &manifests::make_psx,
     .make_ladder = &manifests::make_psx_ladder},
    {.kind = CoreKind::Snes,
     .name = "SNES",
     .profile = &manifests::kSnes,
     .make = &manifests::make_snes,
     .make_companion = &manifests::make_msu_companion},
    {.kind = CoreKind::Sgb,
     .name = "SGB",
     .profile = &manifests::kSgb,
     .make = &manifests::make_sgb,
     .make_companion = &manifests::make_msu_companion},
    {.kind = CoreKind::Electron,
     .name = "AcornElectron",
     .profile = &manifests::kElectron,
     .make = &manifests::make_electron},
    {.kind = CoreKind::AtariSt,
     .name = "AtariST",
     .profile = &manifests::kAtariSt,
     .make = &manifests::make_st,
     .make_ladder = &manifests::make_st_ladder},
    {.kind = CoreKind::ThreeDo,
     .name = "3DO",
     .profile = &manifests::kThreeDo,
     .make = &manifests::make_threedo,
     .make_ladder = &manifests::make_threedo_ladder},
    {.kind = CoreKind::MegaDrive,
     .name = "MegaDrive",
     .profile = &manifests::kMegaDrive,
     .make = &manifests::make_megadrive},
    {.kind = CoreKind::N64,
     .name = "N64",
     .profile = &manifests::kN64,
     .make = &manifests::make_n64,
     .make_loader = &manifests::make_n64_loader},
    {.kind = CoreKind::NeoGeo,
     .name = "NEOGEO",
     .profile = &manifests::kNeoGeo,
     .make = &manifests::make_neogeo,
     .make_ladder = &manifests::make_neogeo_ladder,
     .make_loader = &manifests::make_neogeo_loader},
    {.kind = CoreKind::Ao486,
     .name = "AO486",
     .profile = &manifests::kAo486,
     .make = &make_generic},
    {.kind = CoreKind::Pc110,
     .name = "PC110",
     .profile = &manifests::kPc110,
     .make = &make_generic},
    {.kind = CoreKind::Z486, .name = "Z486", .profile = &manifests::kZ486, .make = &make_generic},
    {.kind = CoreKind::Z386, .name = "Z386", .profile = &manifests::kZ386, .make = &make_generic},
    {.kind = CoreKind::Pcxt, .name = "PCXT", .profile = &manifests::kPcxt, .make = &make_generic},
    {.kind = CoreKind::Tandy1000,
     .name = "Tandy1000",
     .profile = &manifests::kTandy1000,
     .make = &make_generic},
    {.kind = CoreKind::Pcjr, .name = "PCjr", .profile = &manifests::kPcjr, .make = &make_generic},
    {.kind = CoreKind::PcxtEga,
     .name = "PCXT-EGA",
     .profile = &manifests::kPcxtEga,
     .make = &make_generic},
    {.kind = CoreKind::Uneon,
     .name = "Uneon",
     .profile = &manifests::kUneon,
     .make = &make_generic},
    {.kind = CoreKind::Saturn,
     .name = "Saturn",
     .profile = &manifests::kSaturn,
     .make = &manifests::make_saturn,
     .make_ladder = &manifests::make_saturn_ladder},
    {.kind = CoreKind::Cdi,
     .name = "CD-i",
     .profile = &manifests::kCdi,
     .make = &manifests::make_cdi,
     .make_ladder = &manifests::make_cdi_ladder},

    {.kind = CoreKind::Arcade,
     .name = "",
     .profile = &kArcadeProfile,
     .make = &make_arcade,
     .manifest_doc = infra::OptRef<const IManifestDocRole>{kMraRole}},
});

consteval bool table_names_agree() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr)
            return false;
        if (f.kind != f.profile->kind)
            return false;
        std::string_view a = f.name;
        std::string_view b = f.profile->name;
        if (a != b)
            return false;
    }
    return true;
}

static_assert(table_names_agree(), "registry row name/kind must match its CoreProfile");

consteval bool kinds_are_the_rows() {
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        if (kCoreTable[i].kind == CoreKind::Generic)
            return false;
        for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
            if (kCoreTable[i].kind == kCoreTable[j].kind)
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

consteval bool generic_rows_start_no_mount() {
    for (const CoreFactory& f : kCoreTable)
        if (f.make == &make_generic && f.profile->undeclared_start_mount != StartMount::Unmodelled)
            return false;
    return true;
}

static_assert(generic_rows_start_no_mount(), "a make_generic row must not arm an SC start mount");

consteval bool generic_rows_keep_the_class_route() {
    for (const CoreFactory& f : kCoreTable)
        if (f.make == &make_generic && f.profile->undeclared_role != SlotRole::Image)
            return false;
    return true;
}

static_assert(
    generic_rows_keep_the_class_route(),
    "a make_generic row must not route an undeclared slot to the slot service"
);

consteval bool no_row_defaults_to_a_disc() {
    for (const CoreFactory& f : kCoreTable)
        if (f.profile->undeclared_role == SlotRole::Disc)
            return false;
    return true;
}

static_assert(no_row_defaults_to_a_disc(), "no row's undeclared_role may be Disc");

consteval bool block_fact_rows_are_generic(const CoreProfile& p) {
    return !p.is_front_end
        && p.services.empty()
        && p.slots.empty()
        && p.boot_assets.empty()
        && p.start_assets.empty()
        && p.signatures.empty()
        && p.windows.empty()
        && p.staging.order == StageOrder::None
        && !p.suppresses_mgl
        && !p.skips_memsz
        && !p.suppress_reset_status_write
        && !p.suppress_analog_followup
        && !p.analog_reshape
        && p.cue_browse_dir.empty()
        && p.default_uart_baud == 115200
        && p.disk_formats.empty()
        && p.option_rows.empty()
        && p.option_pages.empty()
        && p.init_additions.empty()
        && !p.blank_save.declared();
}

static_assert(
    block_fact_rows_are_generic(manifests::kAo486)
    && block_fact_rows_are_generic(manifests::kPc110)
    && block_fact_rows_are_generic(manifests::kZ486)
    && block_fact_rows_are_generic(manifests::kZ386)
    && block_fact_rows_are_generic(manifests::kPcxt)
    && block_fact_rows_are_generic(manifests::kTandy1000)
    && block_fact_rows_are_generic(manifests::kPcjr)
    && block_fact_rows_are_generic(manifests::kPcxtEga)
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

consteval bool staging_cores_declare_blank_save() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr)
            return false;
        if (f.profile->staging.order == StageOrder::None)
            continue;
        if (!f.profile->blank_save.declared() && !f.profile->blank_save.generic_ff)
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

consteval std::size_t boot_buffers_of(std::span<const BootAsset> rows) {
    std::size_t outside = 0;
    bool group0 = false;
    for (const BootAsset& r : rows) {
        group0 = group0 || r.group == 0;
        outside += r.group == 0 ? 0u : 1u;
    }
    return outside + (group0 ? 1u : 0u);
}

consteval BootAsset row_in(std::uint8_t group) {
    return BootAsset{.name = {}, .where = {}, .group = group};
}

constexpr BootAsset kTwoLayeredRows[] = {row_in(0), row_in(0), row_in(1), row_in(1)};
static_assert(boot_buffers_of(kTwoLayeredRows) == 3);

consteval bool boot_buffers_fit_the_ceiling() {
    for (const CoreFactory& f : kCoreTable) {
        if (boot_buffers_of(f.profile->boot_assets) > kMaxBootBuffers)
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
        if (f.profile->start_assets.size() > kMaxStartAssets)
            return false;
    }
    return true;
}

static_assert(
    start_assets_fit_the_ceiling(),
    "a manifest with more than kMaxStartAssets start assets outgrows "
    "app::InternedFile::kMaxLiveFiles; raise it only with its derivation"
);

consteval bool start_rule_matches_its_rows() {
    for (const CoreFactory& f : kCoreTable)
        if ((f.profile->start == StartRule::Assets) == f.profile->start_assets.empty())
            return false;
    return true;
}

static_assert(
    start_rule_matches_its_rows(),
    "StartRule::Assets is the rule of exactly the rows that declare start_assets"
);

consteval bool no_manifest_row_anchors_core_home() {
    const auto anchored = [](std::span<const BootAsset> rows) consteval {
        for (const BootAsset& r : rows)
            if (r.anchor == AssetAnchor::CoreHome)
                return true;
        return false;
    };
    for (const CoreFactory& f : kCoreTable)
        if (anchored(f.profile->boot_assets) || anchored(f.profile->start_assets))
            return false;
    return true;
}

static_assert(
    no_manifest_row_anchors_core_home(),
    "AssetAnchor::CoreHome is the generic chain's alone; no ladder can resolve it"
);

consteval bool front_end_and_hint_rows_start_nothing() {
    for (const CoreFactory& f : kCoreTable)
        if ((f.profile->is_front_end || f.profile->name.empty())
            && f.profile->start != StartRule::None)
            return false;
    return true;
}

static_assert(
    front_end_and_hint_rows_start_nothing(),
    "the front end and a hint-only row run their own start arm: StartRule::None"
);

consteval bool start_ladder_rows_take_assets() {
    for (const CoreFactory& f : kCoreTable)
        if (!f.profile->staging.start_save_dir.empty() && f.profile->start != StartRule::Assets)
            return false;
    return true;
}

static_assert(
    start_ladder_rows_take_assets(),
    "a start ladder carries declared rows only: a start_save_dir row takes Assets"
);

consteval bool manifest_windows_fit_every_sourced_aperture() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr)
            return false;
        if (f.profile->windows.size() > kMaxCoreWindows)
            return false;
        for (const hal::FpgaAperture& ap : hal::kBoardApertures) {
            if (ap.region.len == 0)
                continue;
            std::array<hal::FabricRegion, kMaxCoreWindows> rows{};
            std::size_t n = 0;
            for (const CoreWindowDecl& w : f.profile->windows)
                rows[n++] = w.region;
            if (!hal::fabric_rows_well_formed(std::span(rows).first(n), ap))
                return false;
        }
        std::size_t feeders = 0;
        for (const CoreWindowDecl& w : f.profile->windows) {
            if (w.region.name == nullptr || *w.region.name == '\0')
                return false;
            feeders += w.feeds_pcm ? 1u : 0u;
        }
        if (feeders > 1)
            return false;
    }
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        for (const CoreWindowDecl& a : kCoreTable[i].profile->windows) {
            for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
                for (const CoreWindowDecl& b : kCoreTable[j].profile->windows) {
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

consteval bool staging_rows_declare_a_ladder() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr)
            return false;
        const bool staged = f.profile->staging.order != StageOrder::None;
        const bool laddered = staged || ladder_rows(f.profile->slots) != 0;
        if (laddered != (f.make_ladder != nullptr))
            return false;
        if (f.profile->reset.ladders != (!staged && laddered))
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

consteval bool laddered_rows_declare_one_disc_slot() {
    for (const CoreFactory& f : kCoreTable) {
        const std::size_t discs = disc_rows(f.profile->slots);
        const bool staged = f.profile->staging.order != StageOrder::None;
        if (discs != (staged ? 1u : 0u))
            return false;
        if (discs == 1 && disc_slot_of(f.profile->slots) != f.profile->staging.disc_slot)
            return false;
    }
    return true;
}

static_assert(
    laddered_rows_declare_one_disc_slot(),
    "a staged row declares exactly one SlotRole::Disc slot and derives "
    "staging.disc_slot from it; any other row declares no Disc slot"
);

consteval bool loader_rows_stream() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.make_loader != nullptr && f.profile->file_tx_whole)
            return false;
    }
    return true;
}

static_assert(
    loader_rows_stream(),
    "a row with a MAIN loader answers the stream role: its profile is not file_tx_whole"
);

consteval bool loader_rows_keep_their_loader() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.make_loader == nullptr)
            continue;
        if (loader_for(&f, 0) != f.make_loader || loader_for(&f, 1) != f.make_loader)
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
        if (f.make_companion != nullptr && f.profile->windows.empty())
            return false;
    }
    return true;
}

static_assert(
    companion_rows_declare_a_window(),
    "a row with a MAIN companion declares the window its sibling file loads into"
);

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

}

std::span<const CoreFactory> core_table() {
    return kCoreTable;
}

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name) {
    for (const CoreFactory& f : kCoreTable) {
        if (f.name.empty())
            continue;
        if (iequal(f.name, conf_str_name))
            return &f;
    }
    return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
}

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name, LoadHint hint) {
    if (hint == LoadHint::XmlManifest) {
        for (const CoreFactory& f : kCoreTable) {
            if (f.kind == CoreKind::Arcade)
                return &f;
        }
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
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

const CoreProfile& profile_for(std::string_view conf_str_name) {
    for (const CoreFactory& f : kCoreTable) {
        if (f.name.empty())
            continue;
        if (iequal(f.name, conf_str_name))
            return *f.profile;
    }
    return kGenericProfile;
}

}
