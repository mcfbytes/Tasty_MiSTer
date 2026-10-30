// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "cores/manifests/n64.h"
#include "cores/n64_path.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"

namespace mister::cores {

using n64::MemoryType;
using n64::PadType;
using n64::SystemType;

namespace {

[[nodiscard]] std::uint8_t hex_to_dec(char c) noexcept {
    if (c >= '0' && c <= '9')
        return static_cast<std::uint8_t>(c - '0');
    if (c >= 'A' && c <= 'F')
        return static_cast<std::uint8_t>(c - 'A' + 10);
    if (c >= 'a' && c <= 'f')
        return static_cast<std::uint8_t>(c - 'a' + 10);
    return 0;
}

[[nodiscard]] bool pad_is_pinned(std::uint32_t v) noexcept {
    const auto p = static_cast<PadType>(v);
    return p == PadType::Snac || p == PadType::Keyboard || p == PadType::Mouse;
}

void set_bit(proto::StatusWord& w, unsigned bit, bool on) noexcept {
    const auto m = static_cast<std::uint16_t>(1u << (bit % 16u));
    auto& word = w.words[bit / 16u];
    word = static_cast<std::uint16_t>(on ? (word | m) : (word & ~m));
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
    if (ask.size != 0 && r == n64::FileRoute::DiskImage)
        return plan_disk_(ask, status);
    if (ask.size != 0 && r == n64::FileRoute::Ipl) {
        LoadPlan p{};
        p.count = 1;
        if (ask.load_addr == 0 || !plan_ipl_row_(p.rows[0], ask.path, true)) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), ask.load_addr});
        }
        p.rows[0].addr = ask.load_addr;
        p.rows[0].index = ask.index;
        p.rows[0].source = ipl_path_.view();
        slab_ = n64::LoadFacts{};
        slab_.bits = status;
        return p;
    }

    if (r == n64::FileRoute::Pif || (ask.size != 0 && r == n64::FileRoute::GameBoy)) {
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
    if (auto b = begin_rom_(ask.size, status); !b) {
        memo_.last_boot.clear();
        return std::unexpected(b.error());
    }
    rom_pending_ = false;
    (void)rom_path_.assign(ask.path);
    p.rows[0].shaped = true;
    p.rows[0].shape = raw(Shape::Rom);

    if (ask.load_addr != 0) {
        p.rows[0].dest = RowDest::Window;
        p.rows[0].addr = ask.load_addr;
    }
    return p;
}

Ex<void> N64Loader::begin_rom_(std::uint64_t size, const proto::StatusWord& status) {

    if (size < n64::kMinRomBytes || size > 0xFFFF'FFFFu) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(size)}
        );
    }
    facts_ = RomFacts{};
    facts_.bytes = size;
    tags_ = n64::DbTags{};
    bootcode_ = {};
    info_.clear();
    md5_ = mra::Md5{};
    slab_ = n64::LoadFacts{};
    slab_.bits = status;
    slab_.bytes = static_cast<std::uint32_t>(size);
    patches_applied_ = 0;
    patch_errors_ = 0;
    concluded_ = false;
    return {};
}

Ex<LoadPlan> N64Loader::plan_disk_(const LoadAsk& ask, const proto::StatusWord& status) {

    if (ask.load_addr == 0)
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
    (void)n64dd::ram_save_path(ask.path, core_dir_, d->ram_path);
    d->load_addr = ask.load_addr;
    disk_ = std::move(d);
    concluded_ = false;

    LoadPlan p{};
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.placed = true;
    row.shape = raw(Shape::Disk);
    row.index = ask.index;
    row.addr = ask.load_addr;
    row.source = disk_->path.view();
    row.file_len = n64dd::kNddBytes;
    row.extent = n64dd::kFlatBytes;
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
        std::string_view root = "boot3.rom";
        if (disk_->area.region == n64dd::Region::Development)
            root = "boot4.rom";
        if (disk_->area.region == n64dd::Region::Usa)
            root = "boot5.rom";
        char rel[N64DbReader::kRelChars];
        if (core_dir_.size() + 1 + root.size() > sizeof rel)
            return;
        std::memcpy(rel, core_dir_.data(), core_dir_.size());
        rel[core_dir_.size()] = '/';
        std::memcpy(rel + core_dir_.size() + 1, root.data(), root.size());
        const std::string_view rel_view(rel, core_dir_.size() + 1 + root.size());
        const svc::SearchPolicy policy{manifests::kN64HomeDir, true};
        auto resolved = vfs_.resolve(rel_view, policy);

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
        row.index = proto::WideIoIndex{(cart ? n64::kDdIplPreloadIndex : n64::kDdIplIndex).v};
        row.addr = manifests::kDdIplLoadAddr;
        row.source = ipl_path_.view();
        ++p.count;
    }
    if (!cart)
        return;
    auto f = vfs_.open(cart_path_.view(), svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size || size->v < n64::kMinRomBytes || size->v > 0xFFFF'FFFFu)
        return;
    cart_bytes_ = size->v;
    rom_pending_ = true;
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = raw(Shape::Rom);
    row.index = proto::WideIoIndex{n64::kRomIndex.v};
    row.addr = manifests::kRomLoadAddr;
    row.source = cart_path_.view();
    row.file_len = static_cast<std::uint32_t>(size->v);
}

bool N64Loader::plan_ipl_row_(TransferRow& row, std::string_view path, bool boot) {
    auto f = vfs_.open(path, svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size || size->v < n64dd::kIplMinBytes || size->v > n64dd::kIplBytes)
        return false;
    if (path.data() != ipl_path_.view().data() && !ipl_path_.assign(path))
        return false;
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = raw(Shape::Ipl);
    row.file_len = static_cast<std::uint32_t>(size->v);
    row.extent = n64dd::kIplBytes;
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
        (void)begin_rom_(cart_bytes_, bits);
        rom_pending_ = false;
        (void)rom_path_.assign(row.source);
    }
    shape_rom_(piece, off);
}

void N64Loader::shape_rom_(std::span<std::uint8_t> piece, std::uint64_t off) {
    if (off == 0)
        facts_.order = n64::detect_byte_order(piece);
    n64::normalize(piece, facts_.order);
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
        n64::normalize(std::span<std::uint8_t>(swap_, n), n64::ByteOrder::ByteSwapped);
        facts_.crc32 = crc32_update(facts_.crc32, std::span<const std::uint8_t>(swap_, n));
    }
}

void N64Loader::shape_ipl_(std::span<std::uint8_t> piece, std::uint64_t off) {
    if (off == 0) {
        ipl_order_ = n64::detect_byte_order(piece);
        slab_.kind = ipl_boot_ ? n64::FactsKind::IplBoot : n64::FactsKind::IplPreload;
        slab_.mask = {};
        slab_.crc32 = 0;
        slab_.bytes = 0;
        concluded_ = false;
    }
    n64::normalize(piece, ipl_order_);
    if (off != 0 || piece.size() < n64::kMinRomBytes)
        return;
    static constexpr std::string_view kDevId = "NDXJ";
    const bool dev = std::memcmp(piece.data() + 0x3B, kDevId.data(), kDevId.size()) == 0;
    write_(n64::opt::kDdDevelopment, dev ? 1u : 0u);
    if (!ipl_boot_)
        return;
    const auto sums = n64::bootcode_checksums(piece.first(n64::kMinRomBytes));
    const auto d = n64::detect_from_bootcode(static_cast<char>(piece[0x3E]), sums);
    if (auto_detect_()) {
        write_(n64::opt::kSysType, static_cast<std::uint32_t>(d.system));
        write_(n64::opt::kCicType, static_cast<std::uint32_t>(d.cic));
        set_cart_save_type_(MemoryType::None);
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

        if (!size || size->v != total)
            return false;
        d.ram_file = std::move(*f);
        d.ram_off = 0;
    }
    if (!d.ram_file)
        return false;
    const auto want = static_cast<std::size_t>(
        std::min<std::uint64_t>(d.ram_buf.size(), total - d.ram_off)
    );
    std::size_t got = 0;
    while (got < want) {
        auto n = d.ram_file->read_at(
            d.ram_off + got,
            std::as_writable_bytes(std::span(d.ram_buf).subspan(got, want - got))
        );
        if (!n || *n == 0) {
            d.ram_file.reset();
            return false;
        }
        got += *n;
    }
    Ex<void> placed{};
    d.map.for_each_ram_run(d.ram_off, want, [&](std::uint32_t flat, std::size_t at, std::size_t n) {
        if (!placed)
            return;
        auto w = window.write(flat, std::as_bytes(std::span(d.ram_buf).subspan(at, n)));
        if (!w)
            placed = std::unexpected(w.error());
    });
    if (!placed)
        return std::unexpected(placed.error());
    d.ram_off += want;
    if (d.ram_off < total)
        return true;
    d.ram_loaded = total;
    d.ram_file.reset();
    return false;
}

Ex<void> N64Loader::finish(ICoreWindow* window) {
    TASTY_SEAT_BODY(N64Loader);
    if (shape_ == Shape::Disk) {

        concluded_ = disk_ && disk_->expander.complete();
        if (concluded_)
            disk_facts_ = n64::DiskFacts{.load_addr = disk_->load_addr};
        return {};
    }
    if (shape_ == Shape::Ipl) {
        concluded_ = true;
        if (ipl_boot_)
            remember_boot_(ipl_path_.view());
        return {};
    }
    facts_.file_md5 = md5_.hex_str();
    run_detect_ladder_();
    apply_patches_(window);
    compose_info_();
    slab_.crc32 = facts_.crc32;
    slab_.kind = n64::FactsKind::Rom;
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
    if (shape_ == Shape::Disk) {
        return infra::byte_cast<const std::uint8_t>(infra::bytes_of(disk_facts_));
    }
    return infra::byte_cast<const std::uint8_t>(infra::bytes_of(slab_));
}

void N64Loader::apply_patches_(ICoreWindow* window) noexcept {
    if (window == nullptr)
        return;
    if (read_(n64::opt::kPatches) != 0)
        return;

    for (const n64::PatchTable::Entry& e : tags_.patches.view().first(tags_.patches.applicable())) {
        const std::uint32_t at = e.address * 4u;
        if (at >= facts_.bytes)
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
    facts_.internal_name = n64::internal_name(head);
    facts_.cart_id = n64::cart_id(head);

    for (std::size_t i = 0; i < 4; ++i)
        facts_.controllers[i] = head[0x34 + i];
    bootcode_ = n64::bootcode_checksums(head);
}

void N64Loader::run_detect_ladder_() {
    facts_.detected = lookup_and_apply_(n64::DbKey::Md5, facts_.header_md5.view());
    if (facts_.detected == 0) {
        facts_.detected = lookup_and_apply_(n64::DbKey::Md5, facts_.file_md5.view());
    }
    if (facts_.detected == 0 && n64::cart_id_lookupable(facts_.cart_id.view())) {
        facts_.detected = lookup_and_apply_(n64::DbKey::CartridgeId, facts_.cart_id.view());
    }
    if (facts_.detected == 0) {
        if (apply_homebrew_()) {
            facts_.homebrew = true;
            facts_.detected = n64::kDetectDbBit;
        } else if (auto_detect_()) {
            apply_unknown_defaults_();
        }
    }

    if ((facts_.detected & n64::kDetectRegionBit) == 0) {
        const std::string_view id = facts_.cart_id.view();
        const char region = id.size() > 3 ? id[3] : '\0';
        const auto d = n64::detect_from_bootcode(region, bootcode_);
        apply_ipl3_(d);
    }
}

std::uint8_t N64Loader::lookup_and_apply_(n64::DbKey kind, std::string_view key) {
    const n64::DbTags t = reader_.scan(kind, key);
    if (!t.found)
        return 0;
    tags_ = t;
    if (!t.malformed)
        apply_tags_(t);
    return n64::detect_level(t, auto_detect_());
}

void N64Loader::apply_tags_(const n64::DbTags& t) {
    if (!auto_detect_())
        return;
    if (t.system != SystemType::Unknown) {
        write_(n64::opt::kSysType, static_cast<std::uint32_t>(t.system));
    }
    if (t.cic != n64::Cic::Unknown) {
        write_(n64::opt::kCicType, static_cast<std::uint32_t>(t.cic));
    }
    write_(n64::opt::kNoEpak, t.no_epak ? 1u : 0u);
    write_(n64::opt::kCpak, t.cpak ? 1u : 0u);
    write_(n64::opt::kRpak, t.rpak ? 1u : 0u);
    write_(n64::opt::kTpak, t.tpak ? 1u : 0u);
    write_(n64::opt::kRtc, t.rtc ? 1u : 0u);
    set_cart_save_type_(t.save_type);

    const std::uint32_t pad0 = read_(n64::opt::kControllers[0]);
    if (auto_pak_() && !pad_is_pinned(pad0)) {
        write_(n64::opt::kControllers[0], static_cast<std::uint32_t>(t.preferred_pad));
    }
}

bool N64Loader::apply_homebrew_() {
    const std::string_view id = facts_.cart_id.view();
    if (id.size() < n64::kCartIdChars)
        return false;
    if (id[1] != 'E' || id[2] != 'D')
        return false;

    if (!auto_detect_())
        return false;

    switch (hex_to_dec(id[4])) {
        case 1:
            set_cart_save_type_(MemoryType::Eeprom512);
            break;
        case 2:
            set_cart_save_type_(MemoryType::Eeprom2k);
            break;
        case 3:
            set_cart_save_type_(MemoryType::Sram32k);
            break;
        case 4:
            set_cart_save_type_(MemoryType::Sram96k);
            break;
        case 5:
            set_cart_save_type_(MemoryType::Flash128k);
            break;
        default:
            set_cart_save_type_(MemoryType::None);
            break;
    }

    write_(n64::opt::kRtc, hex_to_dec(id[5]) & 1u);

    const auto* cs = facts_.controllers;
    const bool rpak = cs[0] == 0x01 || cs[1] == 0x01 || cs[2] == 0x01 || cs[3] == 0x01;
    const bool cpak = cs[0] == 0x02 || cs[1] == 0x02 || cs[2] == 0x02 || cs[3] == 0x02;
    write_(n64::opt::kRpak, rpak ? 1u : 0u);
    write_(n64::opt::kCpak, cpak ? 1u : 0u);

    write_(n64::opt::kTpak, cs[0] == 0x03 ? 1u : 0u);

    if (!auto_pak_())
        return true;
    for (std::size_t i = 0; i < 4; ++i) {
        const std::uint8_t want = cs[i];
        if (want == 0)
            continue;
        if (pad_is_pinned(read_(n64::opt::kControllers[i])))
            continue;
        if (want < 0x80) {
            const PadType p = want == 0x01 ? PadType::PadWithRpak
                : want == 0x02             ? PadType::PadWithCpak
                : (want == 0x03 && i == 0) ? PadType::PadWithTpak
                                           : PadType::Pad;
            write_(n64::opt::kControllers[i], static_cast<std::uint32_t>(p));
        } else if (want == 0xff) {
            write_(n64::opt::kControllers[i], static_cast<std::uint32_t>(PadType::Unplugged));
        }
    }
    return true;
}

void N64Loader::apply_unknown_defaults_() {
    write_(n64::opt::kNoEpak, 0);
    write_(n64::opt::kCpak, 0);
    write_(n64::opt::kRpak, 0);
    write_(n64::opt::kTpak, 0);
    write_(n64::opt::kRtc, 0);
    set_cart_save_type_(MemoryType::None);
}

void N64Loader::apply_ipl3_(const n64::Ipl3Detect& d) {
    if (!auto_detect_()) {

        facts_.detected = static_cast<std::uint8_t>(facts_.detected | n64::kDetectRegionBit);
        return;
    }
    write_(n64::opt::kSysType, static_cast<std::uint32_t>(d.system));
    write_(n64::opt::kCicType, static_cast<std::uint32_t>(d.cic));
    if (d.known_signature) {
        facts_.detected = static_cast<std::uint8_t>(facts_.detected | n64::kDetectRegionBit);
    }
}

void N64Loader::compose_info_() {
    if (!auto_detect_())
        return;
    info_.clear();

    const auto app = [&](std::string_view s) {
        (void)info_.append(s);
    };

    app("Auto-detect:");

    const std::string_view id = facts_.cart_id.view();
    if (!id.empty() && (id[1] != 'E' || id[2] != 'D')) {
        char line[40]{};
        const int n = std::snprintf(
            line,
            sizeof line,
            "\n[%.4s] v.%u.%u",
            facts_.cart_id.c_str(),
            hex_to_dec(id[4]) + 1u,
            hex_to_dec(id[5])
        );
        if (n > 0)
            app(std::string_view(line, static_cast<std::size_t>(n)));
    }
    if (!facts_.internal_name.empty()) {
        app("\n\"");
        app(facts_.internal_name.view());
        app("\"");
    }

    if ((facts_.detected & n64::kDetectRegionBit) == 0) {
        app("\nUnknown Region/CIC");
    } else {
        const auto sys = static_cast<SystemType>(read_(n64::opt::kSysType));
        const auto cic = static_cast<n64::Cic>(read_(n64::opt::kCicType));
        app("\nRegion: ");
        app(n64::system_text(sys));
        app(" (");
        app(n64::cic_text(cic));
        app(")");
    }

    if ((facts_.detected & n64::kDetectDbBit) == 0) {
        app("\nROM missing from database.\nYou might not be able to save.");
    } else {
        const MemoryType save = cart_save_type_();
        if (save != MemoryType::None) {
            app("\nSave Type: ");
            app(n64::save_type_text(save));
        }
        if (read_(n64::opt::kTpak) != 0)
            app("\nTransfer Pak \x96");
        if (read_(n64::opt::kCpak) != 0)
            app("\nController Pak \x96");
        if (read_(n64::opt::kRpak) != 0)
            app("\nRumble Pak \x96");
        if (read_(n64::opt::kRtc) != 0)
            app("\nRTC \x96");
        if (read_(n64::opt::kNoEpak) != 0)
            app("\nDisable Exp. Pak \x96");
        if (patches_applied_ != 0)
            app("\nPatched \x96");
    }
}

std::uint32_t N64Loader::read_(n64::StatusField f) const noexcept {
    std::uint32_t v = 0;
    for (std::uint8_t i = 0; i < f.width; ++i) {
        if (slab_.bits.get_bit(proto::StatusBit{static_cast<std::uint8_t>(f.lo + i)})) {
            v |= (1u << i);
        }
    }
    return v;
}

void N64Loader::write_(n64::StatusField f, std::uint32_t v) noexcept {
    for (std::uint8_t i = 0; i < f.width; ++i) {
        const unsigned bit = f.lo + i;
        set_bit(slab_.bits, bit, ((v >> i) & 1u) != 0u);
        set_bit(slab_.mask, bit, true);
    }
}

bool N64Loader::auto_detect_() const noexcept {

    return read_(n64::opt::kAutoDetect) == 0;
}

bool N64Loader::auto_pak_() const noexcept {
    return read_(n64::opt::kAutoPak) == 0;
}

n64::MemoryType N64Loader::cart_save_type_() const noexcept {
    const auto v = static_cast<MemoryType>(read_(n64::opt::kSaveType));
    return n64::save_size(v) != 0 ? v : MemoryType::None;
}

void N64Loader::set_cart_save_type_(n64::MemoryType t) noexcept {
    const std::uint32_t v = n64::save_size(t) != 0 ? static_cast<std::uint32_t>(t) : 0u;
    write_(n64::opt::kSaveType, v);
}

}
