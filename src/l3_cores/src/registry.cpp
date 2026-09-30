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
#include "hal/board_profile.h"

namespace mister::cores {
namespace {

constexpr auto kCoreTable = std::to_array<CoreFactory>({
    {CoreKind::Menu, "MENU", &kMenuProfile, &make_generic, nullptr, nullptr},
    {CoreKind::Psx, "PSX", &manifests::kPsx, &manifests::make_psx, &manifests::make_psx_ladder,
     nullptr},
    {CoreKind::Snes, "SNES", &manifests::kSnes, &manifests::make_snes, nullptr, nullptr},
    {CoreKind::MegaDrive, "MegaDrive", &manifests::kMegaDrive, &manifests::make_megadrive, nullptr,
     nullptr},
});

consteval bool table_names_agree() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr) return false;
        if (f.kind != f.profile->kind) return false;
        std::string_view a = f.name;
        std::string_view b = f.profile->name;
        if (a != b) return false;
    }
    return true;
}
static_assert(table_names_agree(), "registry row name/kind must match its CoreProfile");

consteval bool kinds_are_the_rows() {
    for (std::size_t i = 0; i < kCoreTable.size(); ++i) {
        if (kCoreTable[i].kind == CoreKind::Generic) return false;
        for (std::size_t j = i + 1; j < kCoreTable.size(); ++j) {
            if (kCoreTable[i].kind == kCoreTable[j].kind) return false;
        }
    }
    return kCoreTable.size() + 1 == std::to_underlying(CoreKind::Count);
}

consteval bool block_fact_rows_are_generic(const CoreProfile& p) {
    return !p.is_front_end && p.services.empty() && p.slots.empty() && p.boot_assets.empty() &&
           p.start_assets.empty() && p.signatures.empty() && p.windows.empty() &&
           p.staging.order == StageOrder::None && !p.suppresses_mgl && !p.skips_memsz &&
           !p.suppress_reset_status_write && !p.suppress_analog_followup &&
           p.analog_reshape == nullptr && p.cue_browse_dir == nullptr &&
           p.default_uart_baud == 115200 && p.disk_formats.empty() && p.option_rows.empty() &&
           p.option_pages.empty() && p.init_additions.empty() && !p.blank_save.declared();
}

consteval bool staging_cores_declare_blank_save() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr) return false;
        if (f.profile->staging.order == StageOrder::None) continue;
        if (!f.profile->blank_save.declared() && !f.profile->blank_save.generic_ff) return false;
    }
    return true;
}
static_assert(staging_cores_declare_blank_save(),
              "a CoreProfile with a staging ladder mounts a deferred-create "
              "save slot, so it MUST declare blank_save: without it every "
              "read of a fresh save is answered 0xFF and the core reads its "
              "backup RAM as corrupt (pass BS / row item)");

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
        if (boot_buffers_of(f.profile->boot_assets) > kMaxBootBuffers) return false;
    }
    return true;
}
static_assert(boot_buffers_fit_the_ceiling(),
              "a manifest whose ladder can load more than kMaxBootBuffers boot assets "
              "outgrows FileBytes::kMaxLiveFiles; raise it only with its derivation");
consteval bool start_assets_fit_the_ceiling() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile->start_assets.size() > kMaxStartAssets) return false;
    }
    return true;
}
static_assert(start_assets_fit_the_ceiling(),
              "a manifest with more than kMaxStartAssets start assets outgrows "
              "FileBytes::kMaxLiveFiles; raise it only with its derivation");

consteval bool manifest_windows_fit_every_sourced_aperture() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr) return false;
        if (f.profile->windows.size() > kMaxCoreWindows) return false;
        for (const hal::FpgaAperture& ap : hal::kBoardApertures) {
            if (ap.region.len == 0) continue;
            std::array<hal::FabricRegion, kMaxCoreWindows> rows{};
            std::size_t n = 0;
            for (const CoreWindowDecl& w : f.profile->windows)
                rows[n++] = w.region;
            if (!hal::fabric_rows_well_formed(std::span(rows).first(n), ap)) return false;
        }
        std::size_t feeders = 0;
        for (const CoreWindowDecl& w : f.profile->windows) {
            if (w.region.name == nullptr || *w.region.name == '\0') return false;
            feeders += w.feeds_pcm ? 1u : 0u;
        }
        if (feeders > 1) return false;
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
static_assert(manifest_windows_fit_every_sourced_aperture(),
              "a declared core window must fit inside every sourced board aperture, carry "
              "a name, share that name with no other manifest's row, number at most "
              "kMaxCoreWindows per manifest and feed PCM from at most one row");

consteval bool staging_rows_declare_a_ladder() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.profile == nullptr) return false;
        const bool staged = f.profile->staging.order != StageOrder::None;
        if (staged != (f.make_ladder != nullptr)) return false;
    }
    return true;
}
static_assert(staging_rows_declare_a_ladder(),
              "a CoreProfile with a staging order names the boot ladder that "
              "runs beside it, and a profile with no staging order names none");

consteval bool laddered_rows_declare_one_disc_slot() {
    for (const CoreFactory& f : kCoreTable) {
        const std::size_t discs = disc_rows(f.profile->slots);
        if (discs != (f.make_ladder != nullptr ? 1u : 0u)) return false;
        if (discs == 1 && disc_slot_of(f.profile->slots) != f.profile->staging.disc_slot)
            return false;
    }
    return true;
}
static_assert(laddered_rows_declare_one_disc_slot(),
              "a row with a boot ladder declares exactly one SlotRole::Disc slot and derives "
              "staging.disc_slot from it; a row with none declares no Disc slot");

consteval bool loader_rows_stream() {
    for (const CoreFactory& f : kCoreTable) {
        if (f.make_loader != nullptr && f.profile->file_tx_whole) return false;
    }
    return true;
}
static_assert(loader_rows_stream(),
              "a row with a MAIN loader answers the stream role: its profile is not file_tx_whole");

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

}  // namespace

std::span<const CoreFactory> core_table() { return kCoreTable; }

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name) {
    for (const CoreFactory& f : kCoreTable) {
        if (f.name.empty()) continue;
        if (iequal(f.name, conf_str_name)) return &f;
    }
    return std::unexpected(Error{Errc::core_load, ERR_SITE(), 0});
}

[[nodiscard]] Ex<const CoreFactory*> find_core(std::string_view conf_str_name, LoadHint hint) {
    if (hint == LoadHint::XmlManifest) {
        for (const CoreFactory& f : kCoreTable) {
            if (f.kind == CoreKind::Arcade) return &f;
        }
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), 1});
    }
    return find_core(conf_str_name);
}

const CoreProfile& profile_for(std::string_view conf_str_name) {
    for (const CoreFactory& f : kCoreTable) {
        if (f.name.empty()) continue;
        if (iequal(f.name, conf_str_name)) return *f.profile;
    }
    return kGenericProfile;
}

}  // namespace mister::cores
