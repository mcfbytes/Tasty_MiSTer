// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64/n64_core.h"
#include "cores/core_adopt.h"

#include <cstring>
#include <string_view>

#include "cores/n64/n64_load_facts.h"
#include "cores/n64/n64_load_map.h"
#include "cores/n64dd/n64dd_ram_save.h"
#include "cores/save_row_count.h"
#include "cores/n64/save_rows.h"
#include "cores/stream_load_end.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"
#include "infra/path_parts.h"
#include "infra/prelude.h"
#include "proto/link/core_session.h"

namespace mister::cores::n64 {

N64Core::N64Core(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      cheats_(h.clock, h.windows.named(n64::kN64Windows[0].region.name)),
      saves_(h.save_store) {}

using n64::SystemType;

Ex<void> N64Core::do_adopt(const CoreAdopt& a) {

    session_ = infra::OptRef<proto::link::CoreSession>{a.session};
    turbo_core_ = a.declares_turbo;

    if (!core_dir_.assign(profile().name)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    return {};
}

Ex<void> N64Core::do_reset() {
    if (options_().cheats_enabled())
        cheats_.rearm_boot_codes();
    return {};
}

void N64Core::service_cheat_tick() noexcept {
    TASTY_SEAT_BODY(N64Core);
    if (!session_)
        return;

    saves_.note_layout(*this);

    const n64::Options opts = options_();
    const SystemType system = opts.system() == SystemType::Pal ? SystemType::Pal : SystemType::Ntsc;

    const n64::CheatTick t = cheats_.tick(
        {.loaded = loaded_, .system = system, .enabled = opts.cheats_enabled(), .gs_pressed = false}
    );
    if (t == n64::CheatTick::Invalid)
        disable_cheats_();
}

void N64Core::on_status_changed() noexcept {
    TASTY_SEAT_BODY(N64Core);
    saves_.note_layout(*this);
}

Ex<proto::SessionParams> N64Core::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(N64Core);
    const proto::FioIndexByte index = ask.index;
    if (!session_)
        return std::unexpected(index.refusal(Errc::core_load, ERR_SITE()));
    const n64::FileRoute route = n64::stream_route(index.io());
    last_index_ = index;
    ++transfers_;
    bytes_sent_ = 0;
    if (route == n64::FileRoute::Rom) {
        loaded_ = false;
    } else {
        crc32_ = {};
    }

    (void)ext_.assign(infra::suffix_from_last_dot(ask.path));
    proto::SessionParams params{};
    params.ext = ext_.view();
    return params;
}

void N64Core::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(N64Core);
    bytes_sent_ = end.bytes.v;
    if (!end.ok)
        return;
    const n64::FileRoute route = n64::stream_route(end.index.io());
    if (route != n64::FileRoute::Rom && route != n64::FileRoute::Ipl)
        return;

    if (route == n64::FileRoute::Rom || end.index.io() == n64::kDdIplIndex) {
        loaded_ = true;
    }

    if (route == n64::FileRoute::Ipl)
        session_->post(proto::StatusEdit{});
}

Ex<void> N64Core::on_apply_load_facts(std::span<const std::uint8_t> facts, std::string_view image) {

    saves_.forget_plan();
    const auto decoded = n64::LoadFacts::of(facts);
    if (decoded && decoded->kind == n64::FactsKind::Disk) {
        const auto order = n64dd::RamSaveOrder::of(decoded->load_addr, image, core_dir_);
        if (!order) {
            ++facts_refusals_;
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
            );
        }
        disk_ = *order;
        return {};
    }
    if (!session_ || !decoded) {
        ++facts_refusals_;
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
        );
    }
    const n64::LoadFacts& f = *decoded;
    session_->stage(f.edit);
    crc32_ = f.crc32;
    if (f.kind == n64::FactsKind::IplPreload)
        return {};
    if (f.kind == n64::FactsKind::IplBoot) {

        adopt_save_plan_(n64::SaveRows{}, image);
        return {};
    }

    if (auto r = session_->flush_status(); !r)
        return r;

    adopt_save_plan_(f.saves, image);
    return {};
}

void N64Core::adopt_save_plan_(const n64::SaveRows& plan, std::string_view image) {

    infra::IndexedArray<SavePartition, n64::kMaxSaveRows, SaveRow> mounts{};
    const n64::SaveRowCount n = plan.count.fits()
        ? plan.count
        : n64::SaveRowCount::truncating(n64::kMaxSaveRows);
    SaveRow row{};
    for (; row != n.end(); row = row.next())
        mounts[row] = n64::save_row_partition(infra::at_index(plan.kinds, row), plan.cart);

    FixedStr<n64::SaveChannel::kPathChars + 1, StrFit::Clip> path{};
    (void)path.assign(image);
    if (auto r = saves_.plan(*this, path.view(), core_dir_.view(), mounts.prefix(n)); !r) {
        ++save_plan_errors_;
        saves_.clear();
    }
}

n64::Options N64Core::options_() const noexcept {
    return n64::Options{session_->status().value()};
}

void N64Core::disable_cheats_() noexcept {

    session_->post(proto::StatusEdit::field(n64::opt::kCheats, proto::FieldValue{1}));
}

n64::SaveLayoutKey N64Core::layout_key(n64::SaveRowCount rows) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    const n64::Options opts = options_();
    return n64::SaveLayoutKey{opts.cart_save_type(), opts.tpak(), rows};
}

WindowJobBuild N64Core::order_ram_save_(WindowJobOrder& order) noexcept {
    if (!disk_)
        return nullptr;
    order.put(*disk_);
    return n64dd::kRamSaveKind;
}

WindowJobBuild N64Core::on_osd_open(WindowJobOrder& order) noexcept {
    TASTY_SEAT_BODY(N64Core);
    if (!loaded_ || !options_().autosave())
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
        && edge.bit == n64::opt::kSaveBackup.start;
    return save_row ? order_ram_save_(order) : nullptr;
}

bool N64Core::owes_flush(proto::IoIndex index) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    const n64::FileRoute route = n64::stream_route(index);
    return disk_.has_value()
        && (route == n64::FileRoute::DiskImage || route == n64::FileRoute::Rom);
}

WindowJobBuild N64Core::take_flush(proto::IoIndex index, WindowJobOrder& order) noexcept {
    TASTY_SEAT_BODY(N64Core);
    const WindowJobBuild kind = order_ram_save_(order);
    if (n64::stream_route(index) == n64::FileRoute::DiskImage) {
        disk_.reset();
    }
    return kind;
}

}
