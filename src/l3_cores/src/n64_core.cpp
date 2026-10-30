// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_core.h"

#include <cstring>
#include <string_view>

#include "cores/manifests/n64.h"
#include "cores/n64_disk_facts.h"
#include "cores/n64_load_facts.h"
#include "cores/n64dd_ram_save.h"
#include "cores/stream_load_end.h"
#include "infra/endian.h"

namespace mister::cores {

N64Core::N64Core(const CoreProfile& p, const HostServices& h)
    : Core(p, h),
      cheats_(h.clock, h.windows.named(manifests::kN64Windows[0].region.name)),
      saves_(h.vfs) {}

using n64::MemoryType;
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
    const bool pal = read_(n64::opt::kSysType) == static_cast<std::uint32_t>(SystemType::Pal);

    const n64::CheatTick t = cheats_.tick(loaded_, pal, cheats_enabled_(), false);
    if (t == n64::CheatTick::Invalid)
        disable_cheats_();
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
    last_index_ = index.v;
    ++transfers_;
    bytes_sent_ = 0;
    if (route == n64::FileRoute::Rom) {
        loaded_ = false;
    } else {
        crc32_ = 0;
    }
    proto::SessionParams params{};
    params.ext = ext_.view();
    return params;
}

void N64Core::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(N64Core);
    bytes_sent_ = end.bytes;
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
    if (facts.size() == sizeof(n64::DiskFacts)) {
        const auto d = infra::load_native<n64::DiskFacts>(facts.first<sizeof(n64::DiskFacts)>());
        disk_addr_ = d.load_addr;
        disk_path_ = rom_path_;
        return {};
    }
    if (!session_ || facts.size() != sizeof(n64::LoadFacts)) {
        ++facts_refusals_;
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
        );
    }
    const auto f = infra::load_native<n64::LoadFacts>(facts.first<sizeof(n64::LoadFacts)>());
    session_->stage(proto::StatusEdit{.bits = f.bits, .mask = f.mask});
    crc32_ = f.crc32;
    if (f.kind == n64::FactsKind::IplPreload)
        return {};
    if (f.kind == n64::FactsKind::IplBoot) {

        if (!saves_.plan(*this, rom_path_.view(), profile().name, {}))
            ++save_plan_errors_;
        return {};
    }

    if (auto r = session_->flush_status(); !r)
        return r;

    plan_saves_();
    return {};
}

void N64Core::plan_saves_() {
    SavePartition rows[NvramStore::kMaxPartitions]{};
    std::size_t n = 0;

    if (const MemoryType cart = cart_save_type_(); cart != MemoryType::None) {
        rows[n++] = {
            .name = "cart",
            .suffix = n64::save_suffix(cart),
            .length = static_cast<std::uint32_t>(n64::save_size(cart))
        };
    }

    const bool tpak = read_(n64::opt::kTpak) != 0;
    const bool cpak = read_(n64::opt::kCpak) != 0;
    const auto pak_row = [](std::string_view name, MemoryType t, std::uint8_t digit) {
        return SavePartition{
            .name = name,
            .suffix = n64::save_suffix(t),
            .length = static_cast<std::uint32_t>(n64::save_size(t)),
            .index = digit
        };
    };
    std::uint8_t digit = 0;

    if (tpak) {
        rows[n++] = pak_row("tpak", MemoryType::Tpak, ++digit);
    } else if (cpak) {
        rows[n++] = pak_row("pak1", MemoryType::Cpak, ++digit);
    }
    if (cpak) {
        for (const std::string_view name : {"pak2", "pak3", "pak4"}) {
            rows[n++] = pak_row(name, MemoryType::Cpak, ++digit);
        }
    }

    if (auto r = saves_.plan(
            *this,
            rom_path_.view(),
            profile().name,
            std::span<const SavePartition>(rows, n)
        );
        !r) {
        ++save_plan_errors_;
        saves_.clear();
    }
}

std::uint32_t N64Core::read_(n64::StatusField f) const noexcept {
    const proto::StatusRegister& st = session_->status();
    std::uint32_t v = 0;
    for (std::uint8_t i = 0; i < f.width; ++i) {
        if (st.get_bit(proto::StatusBit{static_cast<std::uint8_t>(f.lo + i)}))
            v |= (1u << i);
    }
    return v;
}

bool N64Core::cheats_enabled_() const noexcept {
    return read_(n64::opt::kCheats) == 0;
}

void N64Core::disable_cheats_() noexcept {

    session_->post(
        proto::StatusEdit::field(proto::StatusBit{n64::opt::kCheats.lo}, n64::opt::kCheats.width, 1)
    );
}

std::uint32_t N64Core::save_offset(std::size_t idx) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    return n64::save_offset(idx, cart_save_type_(), read_(n64::opt::kTpak) != 0);
}

std::uint8_t N64Core::layout_key(std::size_t rows) const noexcept {
    TASTY_SEAT_BODY(N64Core);
    return n64::save_layout_key(cart_save_type_(), read_(n64::opt::kTpak) != 0, rows);
}

n64::MemoryType N64Core::cart_save_type_() const noexcept {
    const auto v = static_cast<MemoryType>(read_(n64::opt::kSaveType));
    return n64::save_size(v) != 0 ? v : MemoryType::None;
}

const IWindowJobKind* N64Core::order_ram_save_(std::span<std::byte> order) noexcept {
    if (disk_addr_ == 0)
        return nullptr;
    const bool
        ok = n64dd::write_ram_save_order(order, disk_addr_, disk_path_.view(), profile().name);
    return ok ? &n64dd::kRamSaveKind : nullptr;
}

const IWindowJobKind* N64Core::on_osd_open(std::span<std::byte> order) noexcept {
    TASTY_SEAT_BODY(N64Core);
    if (!loaded_ || read_(n64::opt::kAutosave) != 0)
        return nullptr;
    return order_ram_save_(order);
}

const IWindowJobKind* N64Core::on_reset_edge(
    const proto::ResetEdge& edge,
    std::span<std::byte> order
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
    return disk_addr_ != 0 && (route == n64::FileRoute::DiskImage || route == n64::FileRoute::Rom);
}

const IWindowJobKind* N64Core::take_flush(
    proto::IoIndex index,
    std::span<std::byte> order
) noexcept {
    TASTY_SEAT_BODY(N64Core);
    const IWindowJobKind* kind = order_ram_save_(order);
    if (n64::stream_route(index) == n64::FileRoute::DiskImage) {
        disk_addr_ = 0;
        disk_path_.clear();
    }
    return kind;
}

}
