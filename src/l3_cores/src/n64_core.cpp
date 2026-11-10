// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_core.h"
#include "cores/core_adopt.h"

#include <cstring>
#include <string_view>

#include "cores/manifests/n64.h"
#include "cores/n64_load_facts.h"
#include "cores/n64dd_ram_save.h"
#include "cores/save_row_count.h"
#include "cores/save_rows.h"
#include "cores/stream_load_end.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"
#include "proto/core_session.h"

namespace mister::cores {

N64Core::N64Core(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      cheats_(h.clock, h.windows.named(manifests::kN64Windows[0].region.name)),
      saves_(h.vfs) {}

using n64::SystemType;

Ex<void> N64Core::do_adopt(const CoreAdopt& a) {

    session_ = infra::OptRef<proto::CoreSession>{a.session};
    turbo_core_ = a.declares_turbo;
    return {};
}

Ex<void> N64Core::do_reset() {
    if (cheats_enabled_())
        cheats_.rearm_boot_codes();
    return {};
}

void N64Core::service_cheat_tick() noexcept {
    TASTY_SEAT_BODY(N64Core);
    if (!session_)
        return;

    saves_.note_layout(*this);

    const proto::FieldValue sys_choice = read_(n64::opt::kSysType);
    const proto::FieldValue pal_choice{static_cast<std::uint32_t>(SystemType::Pal)};
    const SystemType system = sys_choice == pal_choice ? SystemType::Pal : SystemType::Ntsc;

    const n64::CheatTick t = cheats_.tick(
        {.loaded = loaded_, .system = system, .enabled = cheats_enabled_(), .gs_pressed = false}
    );
    if (t == n64::CheatTick::Invalid)
        disable_cheats_();
}

void N64Core::on_status_changed() noexcept {
    TASTY_SEAT_BODY(N64Core);
    saves_.note_layout(*this);
}

void N64Core::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
}

void N64Core::on_set_pending_file_path(std::string_view path) noexcept {
    (void)rom_path_.assign(path);
}

Ex<proto::SessionParams> N64Core::stream_opening(IoIndex index) {
    TASTY_SEAT_BODY(N64Core);
    if (!session_)
        return std::unexpected(Error{Errc::core_load, ERR_SITE(), index.v});
    const n64::FileRoute route = n64::stream_route(index);
    last_index_ = index;
    ++transfers_;
    bytes_sent_ = 0;
    if (route == n64::FileRoute::Rom) {
        loaded_ = false;
    } else {
        crc32_ = {};
    }
    proto::SessionParams params{};
    params.ext = ext_.view();
    return params;
}

void N64Core::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(N64Core);
    bytes_sent_ = end.bytes.v;
    if (!end.ok)
        return;
    const n64::FileRoute route = n64::stream_route(end.index);
    if (route != n64::FileRoute::Rom && route != n64::FileRoute::Ipl)
        return;

    if (route == n64::FileRoute::Rom || (end.index.v & 0x3fu) == n64::kDdIplIndex.v) {
        loaded_ = true;
    }

    if (route == n64::FileRoute::Ipl)
        session_->post(proto::StatusEdit{});
}

Ex<void> N64Core::on_apply_load_facts(std::span<const std::uint8_t> facts) {

    saves_.forget_plan();
    const auto decoded = n64::LoadFacts::of(facts);
    if (decoded && decoded->kind == n64::FactsKind::Disk) {
        disk_addr_ = decoded->load_addr;
        disk_path_ = rom_path_;
        return {};
    }
    if (!session_ || !decoded) {
        ++facts_refusals_;
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
        );
    }
    const n64::LoadFacts& f = *decoded;
    session_->stage(proto::StatusEdit{.bits = f.bits, .mask = f.mask});
    crc32_ = f.crc32;
    if (f.kind == n64::FactsKind::IplPreload)
        return {};
    if (f.kind == n64::FactsKind::IplBoot) {

        adopt_save_plan_(n64::SaveRows{});
        return {};
    }

    if (auto r = session_->flush_status(); !r)
        return r;

    adopt_save_plan_(f.saves);
    return {};
}

void N64Core::adopt_save_plan_(const n64::SaveRows& plan) {

    infra::IndexedArray<SavePartition, n64::kMaxSaveRows, SaveRow> mounts{};
    const n64::SaveRowCount n = plan.count.fits() ? plan.count
                                                  : n64::SaveRowCount::of(n64::kMaxSaveRows);
    SaveRow row{};
    for (; row != n.end(); row = row.next())
        mounts[row] = n64::save_row_partition(infra::at_index(plan.kinds, row), plan.cart);

    if (auto r = saves_.plan(*this, rom_path_.view(), profile().name, mounts.prefix(n)); !r) {
        ++save_plan_errors_;
        saves_.clear();
    }
}

proto::FieldValue N64Core::read_(n64::StatusField f) const noexcept {
    return f.read(session_->status().value());
}

bool N64Core::cheats_enabled_() const noexcept {
    return read_(n64::opt::kCheats) == proto::FieldValue{};
}

void N64Core::disable_cheats_() noexcept {

    session_->post(
        proto::StatusEdit::field(
            proto::StatusBit{n64::opt::kCheats.lo},
            n64::opt::kCheats.width,
            proto::FieldValue{1}
        )
    );
}

n64::SaveLayoutKey N64Core::layout_key(n64::SaveRowCount rows) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    return n64::SaveLayoutKey{
        n64::save_type_of(read_(n64::opt::kSaveType)),
        read_(n64::opt::kTpak) != proto::FieldValue{},
        rows
    };
}

WindowJobBuild N64Core::order_ram_save_(WindowJobOrder& order) noexcept {
    if (disk_addr_ == hal::FabricAddr{})
        return nullptr;
    const bool
        ok = n64dd::write_ram_save_order(order, disk_addr_, disk_path_.view(), profile().name);
    return ok ? n64dd::kRamSaveKind : nullptr;
}

WindowJobBuild N64Core::on_osd_open(WindowJobOrder& order) noexcept {
    TASTY_SEAT_BODY(N64Core);
    if (!loaded_ || read_(n64::opt::kAutosave) != proto::FieldValue{})
        return {};
    return order_ram_save_(order);
}

WindowJobBuild N64Core::on_reset_edge(
    const proto::ResetEdge& edge,
    WindowJobOrder& order
) noexcept {
    TASTY_SEAT_BODY(N64Core);
    const bool save_row = edge.source == proto::ResetSource::OsdToggle
        && !edge.ex
        && edge.bit == proto::StatusBit{n64::opt::kSaveBackup.lo};
    return save_row ? order_ram_save_(order) : nullptr;
}

bool N64Core::owes_flush(proto::IoIndex index) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    const n64::FileRoute route = n64::stream_route(index);
    return disk_addr_ != hal::FabricAddr{}
    && (route == n64::FileRoute::DiskImage || route == n64::FileRoute::Rom);
}

WindowJobBuild N64Core::take_flush(proto::IoIndex index, WindowJobOrder& order) noexcept {
    TASTY_SEAT_BODY(N64Core);
    const WindowJobBuild kind = order_ram_save_(order);
    if (n64::stream_route(index) == n64::FileRoute::DiskImage) {
        disk_addr_ = {};
        disk_path_.clear();
    }
    return kind;
}

}
