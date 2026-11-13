// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_loader.h"

#include <limits>

#include "cores/boot_asset.h"
#include "cores/manifests/n64.h"
#include "cores/n64_file_route.h"
#include "cores/n64_path.h"
#include "cores/n64_rom.h"
#include "cores/n64dd_geometry.h"
#include "cores/n64dd_system_area.h"
#include "infra/byte_view.h"
#include "infra/path_parts.h"

namespace mister::cores {

namespace {

[[nodiscard]] bool rom_size_ok(proto::FileSize size) noexcept {
    return size.within(
        proto::FileSize{n64::kMinRomBytes},
        proto::FileSize{std::numeric_limits<std::uint32_t>::max()}
    );
}

}

Ex<LoadPlan> N64Loader::plan(const LoadAsk& ask, const proto::StatusWord& status) {
    TASTY_SEAT_BODY(N64Loader);
    const n64::FileRoute r = n64::file_route(ask.index.io());
    if (ask.size != proto::FileSize{} && r == n64::FileRoute::DiskImage)
        return plan_disk_(ask, status);
    if (ask.size != proto::FileSize{} && r == n64::FileRoute::Ipl) {
        LoadPlan p{};
        p.count = 1;
        if (ask.load_addr == hal::FabricAddr{} || !ipl_.plan_row(p.rows[0], ask.path, true)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ask.load_addr.v});
        }
        p.rows[0].addr = ask.load_addr.v;
        p.rows[0].index = ask.index;
        slab_ = n64::LoadFacts{};
        slab_.edit = proto::StatusEdit::seeded(status);
        concluded_ = false;
        return p;
    }

    if (r == n64::FileRoute::Pif
        || (ask.size != proto::FileSize{} && r == n64::FileRoute::GameBoy)) {
        LoadPlan p{};
        p.count = 1;
        p.rows[0].index = ask.index;
        return p;
    }
    return plan_rom_(ask, status);
}

Ex<LoadPlan> N64Loader::plan_rom_(const LoadAsk& ask, const proto::StatusWord& status) {
    LoadPlan p{};
    p.count = 1;
    p.rows[0].index = ask.index;
    if (!rom_size_ok(ask.size)) {
        memo_.last_boot.clear();
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ask.size.detail()});
    }
    rom_.begin(slab_, ask.size, status, ask.path);
    concluded_ = false;
    p.rows[0].shaped = true;
    p.rows[0].shape = n64::shape_key_of(n64::PassKind::Rom);

    if (ask.load_addr != hal::FabricAddr{}) {
        p.rows[0].dest = RowDest::Window;
        p.rows[0].addr = ask.load_addr.v;
    }
    return p;
}

Ex<LoadPlan> N64Loader::plan_disk_(const LoadAsk& ask, const proto::StatusWord& status) {

    if (ask.load_addr == hal::FabricAddr{})
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    slab_ = n64::LoadFacts{};
    slab_.edit = proto::StatusEdit::seeded(status);
    auto d = n64::DiskPass::open(vfs_, ask, core_dir_);
    if (!d)
        return std::unexpected(d.error());
    disk_ = std::move(*d);
    concluded_ = false;

    LoadPlan p{};
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.placed = true;
    row.shape = n64::shape_key_of(n64::PassKind::Disk);
    row.index = ask.index;
    row.addr = ask.load_addr.v;
    row.source = disk_->path();
    row.file_len = proto::FileSize{n64dd::kNddBytes};
    row.extent = proto::FileSize{n64dd::kFlatBytes};
    plan_autoload_(p);
    return p;
}

void N64Loader::plan_autoload_(LoadPlan& p) {
    const std::string_view disk = disk_->path();

    cart_path_.clear();
    const bool cart = cart_path_.assign(
                          disk.substr(0, disk.size() - infra::extension_any_sep(disk).size())
                      )
        && cart_path_.append(".rom")
        && vfs_.file_exists(cart_path_.view());

    Path ipl_path{};
    bool ok = ipl_path.assign(infra::dir_prefix_any_sep(disk));
    ok = ok && ipl_path.append("dd_bios.rom");
    if (!ok || !vfs_.file_exists(ipl_path.view())) {
        const auto rel = n64::core_relative(core_dir_, n64dd::root_ipl_name(disk_->area().region));
        if (!rel)
            return;
        const svc::SearchPolicy policy{kCoreHomeDir, true};
        auto resolved = vfs_.resolve(rel->view(), policy);

        if (!resolved || n64::same_file(memo_.last_boot.view(), *resolved)) {
            ipl_path.clear();
        } else if (!ipl_path.assign(*resolved)) {
            ipl_path.clear();
        }
    }
    if (!ipl_path.empty()) {
        TransferRow& row = p.rows[p.count];

        if (!ipl_.plan_row(row, ipl_path.view(), !cart))
            return;
        row.index = proto::WideIoIndex::of(cart ? n64::kDdIplPreloadIndex : n64::kDdIplIndex, 0);
        row.addr = manifests::kDdIplLoadAddr;
        ++p.count;
    }
    if (!cart)
        return;
    auto f = vfs_.open(cart_path_.view(), svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size || !rom_size_ok(*size))
        return;
    rom_.defer_cart(*size);
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = n64::shape_key_of(n64::PassKind::Rom);
    row.index = proto::WideIoIndex::of(n64::kRomIndex, 0);
    row.addr = manifests::kRomLoadAddr;
    row.source = cart_path_.view();
    row.file_len = *size;
}

n64::IRowPass* N64Loader::pass_(n64::PassKind kind) noexcept {
    switch (kind) {
        case n64::PassKind::Rom:
            return &rom_;
        case n64::PassKind::Ipl:
            return &ipl_;
        case n64::PassKind::Disk:
            return disk_.get();
    }
    return nullptr;
}

void N64Loader::shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) {
    TASTY_SEAT_BODY(N64Loader);
    const auto kind = n64::pass_kind_of(row.shape);
    n64::IRowPass* const pass = kind ? pass_(*kind) : nullptr;
    if (pass == nullptr) {
        bad_row_ = true;
        return;
    }
    const n64::PiecePlace at = off == 0 ? n64::PiecePlace::First : n64::PiecePlace::Later;
    bad_row_ = false;
    current_ = kind;
    if (at == n64::PiecePlace::First)
        concluded_ = false;
    pass->shape(row, slab_, piece, at);
}

Ex<void> N64Loader::place(
    const TransferRow& row,
    std::span<const std::uint8_t> piece,
    std::uint64_t,
    ICoreWindow& window
) {
    TASTY_SEAT_BODY(N64Loader);
    const auto kind = n64::pass_kind_of(row.shape);
    if (!kind)
        return std::unexpected(row.shape.refused(Errc::bad_format, ERR_SITE()));
    n64::IRowPass* const pass = pass_(*kind);
    if (pass == nullptr)
        return std::unexpected(row.shape.refused(Errc::bad_format, ERR_SITE()));
    bad_row_ = false;
    current_ = kind;
    return pass->place(piece, window);
}

Ex<bool> N64Loader::conclude(const TransferRow& row, ICoreWindow& window) {
    TASTY_SEAT_BODY(N64Loader);
    const auto kind = n64::pass_kind_of(row.shape);
    if (!kind)
        return std::unexpected(row.shape.refused(Errc::bad_format, ERR_SITE()));
    n64::IRowPass* const pass = pass_(*kind);
    if (pass == nullptr)
        return false;
    bad_row_ = false;
    current_ = kind;
    return pass->conclude(window);
}

Ex<void> N64Loader::finish(infra::OptRef<ICoreWindow> window) {
    TASTY_SEAT_BODY(N64Loader);
    n64::IRowPass* const pass = (!bad_row_ && current_) ? pass_(*current_) : nullptr;
    if (pass == nullptr)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const Ex<bool> done = pass->finish(slab_, window);
    if (!done)
        return std::unexpected(done.error());
    concluded_ = *done;
    return {};
}

std::span<const std::uint8_t> N64Loader::facts() const noexcept {
    TASTY_SEAT_BODY(N64Loader);
    if (!concluded_)
        return {};
    return infra::byte_cast<const std::uint8_t>(infra::bytes_of(slab_));
}

}
