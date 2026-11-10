// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_loader.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "cores/manifests/n64.h"
#include "cores/n64_path.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"

namespace mister::cores {

using n64::MemoryType;

namespace {

[[nodiscard]] bool rom_size_ok(proto::FileSize size) noexcept {
    return size.within(
        proto::FileSize{n64::kMinRomBytes},
        proto::FileSize{std::numeric_limits<std::uint32_t>::max()}
    );
}

}

N64Loader::Shape N64Loader::shape_of(const TransferRow& row) noexcept {
    switch (row.shape) {
        case raw(Shape::Ipl):
            return Shape::Ipl;
        case raw(Shape::Disk):
            return Shape::Disk;
        default:
            return Shape::Rom;
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
        if (ask.load_addr == hal::FabricAddr{} || !plan_ipl_row_(p.rows[0], ask.path, true)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ask.load_addr.v});
        }
        p.rows[0].addr = ask.load_addr.v;
        p.rows[0].index = ask.index;
        p.rows[0].source = ipl_path_.view();
        slab_ = n64::LoadFacts{};
        slab_.bits = status;
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
    begin_rom_(ask.size, status);
    rom_pending_ = false;
    (void)rom_path_.assign(ask.path);
    p.rows[0].shaped = true;
    p.rows[0].shape = raw(Shape::Rom);

    if (ask.load_addr != hal::FabricAddr{}) {
        p.rows[0].dest = RowDest::Window;
        p.rows[0].addr = ask.load_addr.v;
    }
    return p;
}

void N64Loader::begin_rom_(proto::FileSize size, const proto::StatusWord& status) {
    facts_ = RomFacts{};
    facts_.bytes = size;
    ladder_.reset();
    md5_ = mra::Md5{};
    slab_ = n64::LoadFacts{};
    slab_.bits = status;
    slab_.bytes = size;
    patches_applied_ = 0;
    patch_errors_ = 0;
    concluded_ = false;
}

Ex<LoadPlan> N64Loader::plan_disk_(const LoadAsk& ask, const proto::StatusWord& status) {

    if (ask.load_addr == hal::FabricAddr{})
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    auto file = vfs_.open(ask.path, svc::OpenMode::Read);
    if (!file)
        return std::unexpected(file.error());
    slab_ = n64::LoadFacts{};
    slab_.bits = status;
    auto d = std::make_unique<DiskLoad>();
    auto area = n64dd::read_system_area(**file);
    if (!area)
        return std::unexpected(area.error());
    d->area = *area;
    if (auto m = n64dd::DiskMap::build_into(d->map, d->area); !m)
        return std::unexpected(m.error());
    d->expander.start(d->map);
    if (!d->path.assign(ask.path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});

    if (const auto ram_path = n64dd::ram_save_path(ask.path, core_dir_))
        d->ram_path = *ram_path;
    d->load_addr = ask.load_addr;
    disk_ = std::move(d);
    concluded_ = false;

    LoadPlan p{};
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.placed = true;
    row.shape = raw(Shape::Disk);
    row.index = ask.index;
    row.addr = ask.load_addr.v;
    row.source = disk_->path.view();
    row.file_len = proto::FileSize{n64dd::kNddBytes};
    row.extent = proto::FileSize{n64dd::kFlatBytes};
    plan_autoload_(p);
    return p;
}

void N64Loader::plan_autoload_(LoadPlan& p) {
    const std::string_view disk = disk_->path.view();

    cart_path_.clear();
    const std::string_view leaf = n64::path_leaf(disk);
    const std::size_t dot = leaf.rfind('.');
    const std::size_t stem = dot == std::string_view::npos ? disk.size()
                                                           : disk.size() - leaf.size() + dot;
    const bool cart = cart_path_.assign(disk.substr(0, stem))
        && cart_path_.append(".rom")
        && vfs_.file_exists(cart_path_.view());

    ipl_path_.clear();
    bool ok = ipl_path_.assign(disk.substr(0, disk.size() - leaf.size()));
    ok = ok && ipl_path_.append("dd_bios.rom");
    if (!ok || !vfs_.file_exists(ipl_path_.view())) {
        const auto rel = n64::core_relative(core_dir_, n64dd::root_ipl_name(disk_->area.region));
        if (!rel)
            return;
        const svc::SearchPolicy policy{kCoreHomeDir, true};
        auto resolved = vfs_.resolve(rel->view(), policy);

        if (!resolved || n64::same_file(memo_.last_boot.view(), *resolved)) {
            ipl_path_.clear();
        } else if (!ipl_path_.assign(*resolved)) {
            ipl_path_.clear();
        }
    }
    if (!ipl_path_.empty()) {
        TransferRow& row = p.rows[p.count];

        if (!plan_ipl_row_(row, ipl_path_.view(), !cart))
            return;
        row.index = proto::WideIoIndex::of(cart ? n64::kDdIplPreloadIndex : n64::kDdIplIndex, 0);
        row.addr = manifests::kDdIplLoadAddr;
        row.source = ipl_path_.view();
        ++p.count;
    }
    if (!cart)
        return;
    auto f = vfs_.open(cart_path_.view(), svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size || !rom_size_ok(*size))
        return;
    cart_bytes_ = *size;
    rom_pending_ = true;
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = raw(Shape::Rom);
    row.index = proto::WideIoIndex::of(n64::kRomIndex, 0);
    row.addr = manifests::kRomLoadAddr;
    row.source = cart_path_.view();
    row.file_len = *size;
}

bool N64Loader::plan_ipl_row_(TransferRow& row, std::string_view path, bool boot) {
    auto f = vfs_.open(path, svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size
        || !size->within(proto::FileSize{n64dd::kIplMinBytes}, proto::FileSize{n64dd::kIplBytes})) {
        return false;
    }
    if (path.data() != ipl_path_.view().data() && !ipl_path_.assign(path))
        return false;
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = raw(Shape::Ipl);
    row.file_len = *size;
    row.extent = proto::FileSize{n64dd::kIplBytes};
    row.fill = 0;
    row.mirror = boot ? manifests::kRomLoadAddr : 0u;
    ipl_boot_ = boot;
    return true;
}

void N64Loader::shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) {
    TASTY_SEAT_BODY(N64Loader);
    shape_ = shape_of(row);
    if (shape_ == Shape::Ipl) {
        shape_ipl_(piece, off);
        return;
    }
    if (off == 0 && rom_pending_) {

        const proto::StatusWord bits = slab_.bits;
        begin_rom_(cart_bytes_, bits);
        rom_pending_ = false;
        (void)rom_path_.assign(row.source);
    }
    shape_rom_(piece, off);
}

void N64Loader::shape_rom_(std::span<std::uint8_t> piece, std::uint64_t off) {
    if (off == 0) {
        facts_.order = n64::detect_byte_order(piece);
        rom_rule_ = word_order_for_(facts_.order);
    }
    normalize_(piece, rom_rule_);
    if (off == 0) {
        const std::span<const std::uint8_t> head = piece.first(
            std::min(piece.size(), kHeaderBytes)
        );
        md5_.update(head);
        mra::Md5 header = md5_;
        facts_.header_md5 = header.hex_str();
        analyze_header_(head);
        md5_.update(piece.subspan(head.size()));
    } else {
        md5_.update(piece);
    }

    for (std::size_t at = 0; at < piece.size(); at += kHeaderBytes) {
        const std::size_t n = std::min(kHeaderBytes, piece.size() - at);
        std::memcpy(swap_, piece.data() + at, n);
        halfword_swap_.normalize(std::span<std::uint8_t>(swap_, n));
        facts_.crc32 = svc::crc32_extend(facts_.crc32, std::span<const std::uint8_t>(swap_, n));
    }
}

infra::OptRef<const n64::IWordOrder> N64Loader::word_order_for_(n64::ByteOrder order) noexcept {
    switch (order) {
        case n64::ByteOrder::Big:
        case n64::ByteOrder::Unknown:
            break;
        case n64::ByteOrder::ByteSwapped:
            return infra::OptRef<const n64::IWordOrder>{halfword_swap_};
        case n64::ByteOrder::Little:
            return infra::OptRef<const n64::IWordOrder>{word_reverse_};
    }
    return {};
}

void N64Loader::normalize_(
    std::span<std::uint8_t> data,
    infra::OptRef<const n64::IWordOrder> rule
) noexcept {
    if (rule)
        rule->normalize(data);
}

void N64Loader::shape_ipl_(std::span<std::uint8_t> piece, std::uint64_t off) {
    if (off == 0) {
        ipl_rule_ = word_order_for_(n64::detect_byte_order(piece));
        slab_.kind = ipl_boot_ ? n64::FactsKind::IplBoot : n64::FactsKind::IplPreload;
        slab_.mask = {};
        slab_.crc32 = {};
        slab_.bytes = proto::FileSize{};
        concluded_ = false;
    }
    normalize_(piece, ipl_rule_);
    if (off != 0 || piece.size() < n64::kMinRomBytes)
        return;
    static constexpr std::string_view kDevId = "NDXJ";
    const bool dev = std::memcmp(piece.data() + n64::kCartIdOffset, kDevId.data(), kDevId.size())
        == 0;
    slab_.set_flag(n64::opt::kDdDevelopment, dev);
    if (!ipl_boot_)
        return;
    const auto sums = bootcode_sum_.sums(piece.first(n64::kMinRomBytes));
    const auto d = n64::detect_from_bootcode(
        static_cast<char>(piece[n64::kCartIdRegionOffset]),
        sums
    );
    if (slab_.auto_detect()) {
        slab_.set_system(d.system);
        slab_.set_cic(d.cic);
        slab_.set_cart_save_type(MemoryType::None);
    }
}

Ex<void> N64Loader::place(
    const TransferRow& row,
    std::span<const std::uint8_t> piece,
    std::uint64_t,
    ICoreWindow& window
) {
    TASTY_SEAT_BODY(N64Loader);
    shape_ = shape_of(row);
    if (shape_ != Shape::Disk || !disk_) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), row.shape});
    }
    return disk_->expander.feed(piece, window);
}

Ex<bool> N64Loader::conclude(const TransferRow& row, ICoreWindow& window) {
    TASTY_SEAT_BODY(N64Loader);
    if (shape_of(row) != Shape::Disk || !disk_)
        return false;

    if (!disk_->expander.complete())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return load_ram_step_(window);
}

Ex<bool> N64Loader::load_ram_step_(ICoreWindow& window) {
    DiskLoad& d = *disk_;
    const std::uint32_t total = d.map.ram_bytes();
    if (!d.ram_checked) {
        d.ram_checked = true;
        if (total == 0 || d.ram_path.empty() || !vfs_.file_exists(d.ram_path.view()))
            return false;
        auto f = vfs_.open(d.ram_path.view(), svc::OpenMode::ReadWhole);
        if (!f)
            return false;
        auto size = (*f)->size();

        if (!size || *size != proto::FileSize{total})
            return false;
        d.ram_file = std::move(*f);
        d.ram_off = 0;
    }
    if (!d.ram_file)
        return false;
    const auto want = static_cast<std::size_t>(
        std::min<std::uint64_t>(d.ram_buf.size(), total - d.ram_off)
    );
    std::span<std::uint8_t> chunk(d.ram_buf);
    if (auto r = svc::read_exact(
            *d.ram_file,
            proto::FileSize{d.ram_off},
            std::as_writable_bytes(chunk.first(want))
        );
        !r) {
        d.ram_file.reset();
        return false;
    }
    Ex<void> placed{};
    d.map.for_each_ram_run(
        d.ram_off,
        want,
        [&](n64dd::FlatOffset flat, std::size_t at, std::size_t n) {
            if (!placed)
                return;
            auto w = flat.write_via(window, std::as_bytes(std::span(d.ram_buf).subspan(at, n)));
            if (!w)
                placed = std::unexpected(w.error());
        }
    );
    if (!placed)
        return std::unexpected(placed.error());
    d.ram_off += want;
    if (d.ram_off < total)
        return true;
    d.ram_loaded = total;
    d.ram_file.reset();
    return false;
}

Ex<void> N64Loader::finish(infra::OptRef<ICoreWindow> window) {
    TASTY_SEAT_BODY(N64Loader);
    if (shape_ == Shape::Disk) {

        concluded_ = disk_ && disk_->expander.complete();
        if (concluded_) {
            slab_ = n64::LoadFacts{};
            slab_.kind = n64::FactsKind::Disk;
            slab_.load_addr = disk_->load_addr;
        }
        return {};
    }
    if (shape_ == Shape::Ipl) {
        concluded_ = true;
        if (ipl_boot_)
            remember_boot_(ipl_path_.view());
        return {};
    }
    facts_.file_md5 = md5_.hex_str();
    const n64::DetectOutcome detected = ladder_.run(facts_, slab_);
    facts_.detected = detected.detected;
    facts_.homebrew = detected.homebrew;
    apply_patches_(window);
    ladder_.compose_info(facts_, slab_, patches_applied_ != 0);
    slab_.crc32 = facts_.crc32;
    slab_.kind = n64::FactsKind::Rom;

    proto::StatusWord fresh = slab_.bits;
    if (status_cell_) {
        const proto::StatusCell::Sample sample = status_cell_->sample();
        if (sample)
            fresh = sample.value;
    }
    slab_.saves = slab_.save_plan(fresh);
    concluded_ = true;
    remember_boot_(rom_path_.view());
    return {};
}

void N64Loader::remember_boot_(std::string_view path) noexcept {
    if (!path.empty())
        (void)memo_.last_boot.assign(path);
}

std::span<const std::uint8_t> N64Loader::facts() const noexcept {
    TASTY_SEAT_BODY(N64Loader);
    if (!concluded_)
        return {};
    return infra::byte_cast<const std::uint8_t>(infra::bytes_of(slab_));
}

void N64Loader::apply_patches_(infra::OptRef<ICoreWindow> window) noexcept {
    if (!window)
        return;
    if (slab_.flag(n64::opt::kPatches))
        return;
    const auto& match = ladder_.match();
    if (!match || !match->tags)
        return;
    const n64::PatchTable& patches = match->tags->patches;

    for (const n64::PatchTable::Entry& e : patches.view().first(patches.applicable())) {
        const std::uint32_t at = e.address * 4u;
        if (proto::FileSize{at} >= facts_.bytes)
            break;
        if (e.turbo_core_only && !turbo_)
            continue;
        std::array<std::byte, 4> word{};
        if (auto r = window->read(at, word); !r) {
            ++patch_errors_;
            break;
        }
        for (std::size_t i = 0; i < word.size(); ++i) {
            word[i] ^= std::byte{static_cast<std::uint8_t>(e.diff >> (8 * i))};
        }
        if (auto r = window->write(at, word); !r) {
            ++patch_errors_;
            break;
        }
        ++patches_applied_;
    }
}

void N64Loader::analyze_header_(std::span<const std::uint8_t> head) {
    if (const auto header = n64::rom_header_of(head)) {
        facts_.internal_name = n64::internal_name(*header);
        facts_.cart_id = n64::cart_id(*header);

        for (std::size_t i = 0; i < n64::kControllerCount; ++i)
            facts_.controllers[i] = (*header)[n64::kControllerOffset + i];
    }
    facts_.bootcode = bootcode_sum_.sums(head);
}

}
