// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "cores/manifests/n64.h"
#include "cores/n64_path.h"
#include "infra/byte_view.h"
#include "infra/crc32.h"

namespace mister::cores {

using n64::MemoryType;
using n64::PadType;

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

[[nodiscard]] bool pad_is_pinned(PadType p) noexcept {
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
    if (auto b = begin_rom_(ask.size.v, status); !b) {
        memo_.last_boot.clear();
        return std::unexpected(b.error());
    }
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

Ex<void> N64Loader::begin_rom_(std::uint64_t size, const proto::StatusWord& status) {

    if (size < n64::kMinRomBytes || size > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(size)}
        );
    }
    facts_ = RomFacts{};
    facts_.bytes = proto::FileSize{size};
    ladder_.reset();
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
    row.addr = ask.load_addr.v;
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
        row.index = proto::WideIoIndex::of(cart ? n64::kDdIplPreloadIndex : n64::kDdIplIndex, 0);
        row.addr = manifests::kDdIplLoadAddr;
        row.source = ipl_path_.view();
        ++p.count;
    }
    if (!cart)
        return;
    auto f = vfs_.open(cart_path_.view(), svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size
        || !size->within(
            proto::FileSize{n64::kMinRomBytes},
            proto::FileSize{std::numeric_limits<std::uint32_t>::max()}
        )) {
        return;
    }
    cart_bytes_ = size->v;
    rom_pending_ = true;
    TransferRow& row = p.rows[p.count++];
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = raw(Shape::Rom);
    row.index = proto::WideIoIndex::of(n64::kRomIndex, 0);
    row.addr = manifests::kRomLoadAddr;
    row.source = cart_path_.view();
    row.file_len = static_cast<std::uint32_t>(size->v);
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
        facts_.crc32 = svc::crc32_extend(facts_.crc32, std::span<const std::uint8_t>(swap_, n));
    }
}

void N64Loader::shape_ipl_(std::span<std::uint8_t> piece, std::uint64_t off) {
    if (off == 0) {
        ipl_order_ = n64::detect_byte_order(piece);
        slab_.kind = ipl_boot_ ? n64::FactsKind::IplBoot : n64::FactsKind::IplPreload;
        slab_.mask = {};
        slab_.crc32 = {};
        slab_.bytes = 0;
        concluded_ = false;
    }
    n64::normalize(piece, ipl_order_);
    if (off != 0 || piece.size() < n64::kMinRomBytes)
        return;
    static constexpr std::string_view kDevId = "NDXJ";
    const bool dev = std::memcmp(piece.data() + n64::kCartIdOffset, kDevId.data(), kDevId.size())
        == 0;
    slab_.set_flag(n64::opt::kDdDevelopment, dev);
    if (!ipl_boot_)
        return;
    const auto sums = n64::bootcode_checksums(piece.first(n64::kMinRomBytes));
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

void N64Loader::apply_patches_(ICoreWindow* window) noexcept {
    if (window == nullptr)
        return;
    if (slab_.flag(n64::opt::kPatches))
        return;

    for (const n64::PatchTable::Entry& e :
         ladder_.tags().patches.view().first(ladder_.tags().patches.applicable())) {
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
    facts_.bootcode = n64::bootcode_checksums(head);
}

namespace n64 {

DetectOutcome DetectLadder::run(const RomFacts& facts, LoadFacts& slab) {
    detected_ = lookup_and_apply_(slab, DbKey::Md5, facts.header_md5.view());
    if (detected_.none())
        detected_ = lookup_and_apply_(slab, DbKey::Md5, facts.file_md5.view());
    if (detected_.none() && cart_id_lookupable(facts.cart_id.view())) {
        detected_ = lookup_and_apply_(slab, DbKey::CartridgeId, facts.cart_id.view());
    }
    DetectOutcome out{};
    if (detected_.none()) {
        if (apply_homebrew_(facts, slab)) {
            out.homebrew = true;
            detected_ = DetectFlags{}.with(DetectFlag::Db);
        } else if (slab.auto_detect()) {
            apply_unknown_defaults_(slab);
        }
    }

    if (!detected_.has(DetectFlag::Region)) {
        const std::string_view id = facts.cart_id.view();
        const char region = id.size() > 3 ? id[3] : '\0';
        const auto d = detect_from_bootcode(region, facts.bootcode);
        apply_ipl3_(slab, d, detected_);
    }
    out.detected = detected_;
    return out;
}

DetectFlags DetectLadder::lookup_and_apply_(LoadFacts& slab, DbKey kind, std::string_view key) {
    const DbTags t = reader_.scan(kind, key);
    if (!t.found)
        return {};
    tags_ = t;
    if (!t.malformed)
        apply_tags_(slab, t);
    return DetectFlags::of(detect_level(t, slab.auto_detect()));
}

void DetectLadder::apply_tags_(LoadFacts& slab, const DbTags& t) {
    if (!slab.auto_detect())
        return;
    if (t.system != SystemType::Unknown) {
        slab.set_system(t.system);
    }
    if (t.cic != Cic::Unknown) {
        slab.set_cic(t.cic);
    }
    slab.set_flag(opt::kNoEpak, t.no_epak);
    slab.set_flag(opt::kCpak, t.cpak);
    slab.set_flag(opt::kRpak, t.rpak);
    slab.set_flag(opt::kTpak, t.tpak);
    slab.set_flag(opt::kRtc, t.rtc);
    slab.set_cart_save_type(t.save_type);

    if (slab.auto_pak() && !pad_is_pinned(slab.controller(opt::kControllers[0]))) {
        slab.set_controller(opt::kControllers[0], t.preferred_pad);
    }
}

bool DetectLadder::apply_homebrew_(const RomFacts& facts, LoadFacts& slab) {
    const std::string_view id = facts.cart_id.view();
    if (id.size() < kCartIdChars)
        return false;
    if (id[1] != 'E' || id[2] != 'D')
        return false;

    if (!slab.auto_detect())
        return false;

    switch (hex_to_dec(id[4])) {
        case 1:
            slab.set_cart_save_type(MemoryType::Eeprom512);
            break;
        case 2:
            slab.set_cart_save_type(MemoryType::Eeprom2k);
            break;
        case 3:
            slab.set_cart_save_type(MemoryType::Sram32k);
            break;
        case 4:
            slab.set_cart_save_type(MemoryType::Sram96k);
            break;
        case 5:
            slab.set_cart_save_type(MemoryType::Flash128k);
            break;
        default:
            slab.set_cart_save_type(MemoryType::None);
            break;
    }

    slab.set_flag(opt::kRtc, (hex_to_dec(id[5]) & 1u) != 0u);

    const auto& cs = facts.controllers;
    const bool rpak = cs[0] == 0x01 || cs[1] == 0x01 || cs[2] == 0x01 || cs[3] == 0x01;
    const bool cpak = cs[0] == 0x02 || cs[1] == 0x02 || cs[2] == 0x02 || cs[3] == 0x02;
    slab.set_flag(opt::kRpak, rpak);
    slab.set_flag(opt::kCpak, cpak);

    slab.set_flag(opt::kTpak, cs[0] == 0x03);

    if (!slab.auto_pak())
        return true;
    for (std::size_t i = 0; i < cs.size(); ++i) {
        const std::uint8_t want = cs[i];
        if (want == 0)
            continue;
        if (pad_is_pinned(slab.controller(opt::kControllers[i])))
            continue;
        if (want < 0x80) {
            const PadType p = want == 0x01 ? PadType::PadWithRpak
                : want == 0x02             ? PadType::PadWithCpak
                : (want == 0x03 && i == 0) ? PadType::PadWithTpak
                                           : PadType::Pad;
            slab.set_controller(opt::kControllers[i], p);
        } else if (want == 0xff) {
            slab.set_controller(opt::kControllers[i], PadType::Unplugged);
        }
    }
    return true;
}

void DetectLadder::apply_unknown_defaults_(LoadFacts& slab) {
    slab.set_flag(opt::kNoEpak, false);
    slab.set_flag(opt::kCpak, false);
    slab.set_flag(opt::kRpak, false);
    slab.set_flag(opt::kTpak, false);
    slab.set_flag(opt::kRtc, false);
    slab.set_cart_save_type(MemoryType::None);
}

void DetectLadder::apply_ipl3_(LoadFacts& slab, const Ipl3Detect& d, DetectFlags& detected) {
    if (!slab.auto_detect()) {

        detected = detected.with(DetectFlag::Region);
        return;
    }
    slab.set_system(d.system);
    slab.set_cic(d.cic);
    if (d.known_signature)
        detected = detected.with(DetectFlag::Region);
}

void DetectLadder::compose_info(const RomFacts& facts, const LoadFacts& slab, bool patched) {
    if (!slab.auto_detect())
        return;
    info_.clear();

    const auto app = [&](std::string_view s) {
        (void)info_.append(s);
    };

    app("Auto-detect:");

    const std::string_view id = facts.cart_id.view();
    if (!id.empty() && (id[1] != 'E' || id[2] != 'D')) {
        char line[40]{};
        const int n = std::snprintf(
            line,
            sizeof line,
            "\n[%.4s] v.%u.%u",
            facts.cart_id.c_str(),
            hex_to_dec(id[4]) + 1u,
            hex_to_dec(id[5])
        );
        if (n > 0)
            app(std::string_view(line, static_cast<std::size_t>(n)));
    }
    if (!facts.internal_name.empty()) {
        app("\n\"");
        app(facts.internal_name.view());
        app("\"");
    }

    if (!detected_.has(DetectFlag::Region)) {
        app("\nUnknown Region/CIC");
    } else {
        const SystemType sys = slab.system();
        const Cic cic = slab.cic();
        app("\nRegion: ");
        app(system_text(sys));
        app(" (");
        app(cic_text(cic));
        app(")");
    }

    if (!detected_.has(DetectFlag::Db)) {
        app("\nROM missing from database.\nYou might not be able to save.");
    } else {
        const MemoryType save = slab.cart_save_type();
        if (save != MemoryType::None) {
            app("\nSave Type: ");
            app(save_type_text(save));
        }
        if (slab.flag(opt::kTpak))
            app("\nTransfer Pak \x96");
        if (slab.flag(opt::kCpak))
            app("\nController Pak \x96");
        if (slab.flag(opt::kRpak))
            app("\nRumble Pak \x96");
        if (slab.flag(opt::kRtc))
            app("\nRTC \x96");
        if (slab.flag(opt::kNoEpak))
            app("\nDisable Exp. Pak \x96");
        if (patched)
            app("\nPatched \x96");
    }
}

std::uint32_t LoadFacts::field_(StatusField f) const noexcept {
    std::uint32_t v = 0;
    for (std::uint8_t i = 0; i < f.width; ++i) {
        if (bits.get_bit(proto::StatusBit{static_cast<std::uint8_t>(f.lo + i)})) {
            v |= (1u << i);
        }
    }
    return v;
}

void LoadFacts::set_field_(StatusField f, std::uint32_t v) noexcept {
    for (std::uint8_t i = 0; i < f.width; ++i) {
        const unsigned bit = f.lo + i;
        set_bit(bits, bit, ((v >> i) & 1u) != 0u);
        set_bit(mask, bit, true);
    }
}

bool LoadFacts::flag(StatusField f) const noexcept {
    return field_(f) != 0;
}

void LoadFacts::set_flag(StatusField f, bool on) noexcept {
    set_field_(f, on ? 1u : 0u);
}

SystemType LoadFacts::system() const noexcept {
    return static_cast<SystemType>(field_(opt::kSysType));
}

void LoadFacts::set_system(SystemType t) noexcept {
    set_field_(opt::kSysType, static_cast<std::uint32_t>(t));
}

Cic LoadFacts::cic() const noexcept {
    return static_cast<Cic>(field_(opt::kCicType));
}

void LoadFacts::set_cic(Cic c) noexcept {
    set_field_(opt::kCicType, static_cast<std::uint32_t>(c));
}

PadType LoadFacts::controller(StatusField f) const noexcept {
    return static_cast<PadType>(field_(f));
}

void LoadFacts::set_controller(StatusField f, PadType p) noexcept {
    set_field_(f, static_cast<std::uint32_t>(p));
}

bool LoadFacts::auto_detect() const noexcept {

    return !flag(opt::kAutoDetect);
}

bool LoadFacts::auto_pak() const noexcept {
    return !flag(opt::kAutoPak);
}

MemoryType LoadFacts::cart_save_type() const noexcept {
    const auto v = static_cast<MemoryType>(field_(opt::kSaveType));
    return save_size(v) != 0 ? v : MemoryType::None;
}

void LoadFacts::set_cart_save_type(MemoryType t) noexcept {
    const std::uint32_t v = save_size(t) != 0 ? static_cast<std::uint32_t>(t) : 0u;
    set_field_(opt::kSaveType, v);
}

}

}
