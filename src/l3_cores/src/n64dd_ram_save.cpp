// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_ram_save.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "cores/n64_path.h"
#include "infra/endian.h"

namespace mister::cores::n64dd {
namespace {

[[nodiscard]] bool read_exact(svc::IFile& f, std::uint64_t off, std::span<std::byte> dst) {
    std::size_t got = 0;
    while (got < dst.size()) {
        auto n = f.read_at(off + got, dst.subspan(got));
        if (!n || *n == 0)
            return false;
        got += *n;
    }
    return true;
}

[[nodiscard]] Ex<void> write_all(svc::IFile& f, std::uint64_t off, std::span<const std::byte> src) {
    std::size_t put = 0;
    while (put < src.size()) {
        auto n = f.write_at(off + put, src.subspan(put));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(put)});
        put += *n;
    }
    return {};
}

[[nodiscard]] std::uint32_t marker_offset(std::uint32_t k) noexcept {
    const std::uint32_t head = k / (2u * kTracksPerHead);
    const std::uint32_t track = (k / 2u) % kTracksPerHead;
    return flat_offset(track, head, k % 2u, 0) + kDirtyFlagOffset;
}

}

bool ram_save_path(std::string_view disk_path, std::string_view core_dir, SavePath& out) noexcept {
    const std::string_view leaf = n64::path_leaf(disk_path);
    const std::size_t dot = leaf.rfind('.');
    out.clear();
    bool ok = out.assign("saves/");
    ok = ok && out.append(core_dir);
    ok = ok && out.append("/");
    ok = ok && out.append(dot == std::string_view::npos ? leaf : leaf.substr(0, dot));
    ok = ok && out.append(".ram");
    if (!ok)
        out.clear();
    return ok;
}

const RamSaveJob::Kind kRamSaveKind{};

bool write_ram_save_order(
    std::span<std::byte> order,
    std::uint32_t load_addr,
    std::string_view disk_path,
    std::string_view core_dir
) noexcept {
    RamSaveJob::Order o{};
    if (order.size() < sizeof o
        || disk_path.size() > sizeof o.disk_path
        || core_dir.size() > sizeof o.core_dir) {
        return false;
    }
    o.load_addr = load_addr;
    o.path_len = static_cast<std::uint16_t>(disk_path.size());
    o.core_len = static_cast<std::uint8_t>(core_dir.size());
    std::memcpy(o.disk_path, disk_path.data(), disk_path.size());
    std::memcpy(o.core_dir, core_dir.data(), core_dir.size());
    infra::store_native<RamSaveJob::Order>(order.first<sizeof(RamSaveJob::Order)>(), o);
    return true;
}

IWindowJob* RamSaveJob::Kind::build(
    std::span<std::byte> arena,
    std::span<const std::byte> order
) const noexcept {
    TASTY_SEAT_BODY(RamSaveJob::Kind);
    static_assert(sizeof(RamSaveJob) <= kWindowJobBytes);
    static_assert(alignof(RamSaveJob) <= kWindowJobAlign);
    if (order.size() < sizeof(Order) || arena.size() < sizeof(RamSaveJob))
        return nullptr;
    const auto o = infra::load_native<Order>(order.first<sizeof(Order)>());
    if (o.path_len > sizeof o.disk_path || o.core_len > sizeof o.core_dir)
        return nullptr;
    return ::new (static_cast<void*>(arena.data())) RamSaveJob(
        o.load_addr,
        std::string_view(o.disk_path, o.path_len),
        std::string_view(o.core_dir, o.core_len)
    );
}

RamSaveJob::RamSaveJob(
    std::uint32_t load_addr,
    std::string_view disk_path,
    std::string_view core_dir
) noexcept
    : addr_(load_addr) {
    path_ok_ = disk_path_.assign(disk_path) && core_dir_.assign(core_dir);
}

std::uint32_t RamSaveJob::window_addr() const noexcept {
    TASTY_SEAT_BODY(RamSaveJob);
    return addr_;
}

std::uint32_t RamSaveJob::window_len() const noexcept {
    TASTY_SEAT_BODY(RamSaveJob);
    return kFlatBytes;
}

Ex<bool> RamSaveJob::step(ICoreWindow& window, const svc::Vfs& vfs) {
    TASTY_SEAT_BODY(RamSaveJob);
    switch (phase_) {
        case Phase::Open:
            if (auto r = open_(window, vfs); !r)
                return std::unexpected(r.error());
            break;
        case Phase::Compare:
            if (auto r = compare_step_(window); !r)
                return std::unexpected(r.error());
            break;
        case Phase::Write:
            if (auto r = write_step_(window, vfs); !r)
                return std::unexpected(r.error());
            break;
        case Phase::Clear:
            if (auto r = clear_(window); !r)
                return std::unexpected(r.error());
            break;
        case Phase::Done:
            break;
    }
    return phase_ != Phase::Done;
}

Ex<void> RamSaveJob::open_(ICoreWindow& window, const svc::Vfs& vfs) {
    if (!path_ok_)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (window.size() < kFlatBytes)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    work_.emplace();
    Work& w = *work_;
    auto disk = vfs.open(disk_path_.view(), svc::OpenMode::Read);
    if (!disk)
        return std::unexpected(disk.error());
    auto area = read_system_area(**disk);
    if (!area)
        return std::unexpected(area.error());
    w.area = *area;
    if (auto m = DiskMap::build_into(w.map, w.area); !m)
        return std::unexpected(m.error());
    total_ = w.map.ram_bytes();

    if (total_ == 0) {
        phase_ = Phase::Done;
        return {};
    }
    if (!ram_save_path(disk_path_.view(), core_dir_.view(), w.ram_path)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }

    std::array<std::byte, kDirtyMarkerBytes> mark{};
    for (std::uint32_t k = 0; k < kFlatBlocks; ++k) {
        if (auto r = window.read(marker_offset(k), mark); !r)
            return std::unexpected(r.error());
        const bool dirty = std::all_of(mark.begin(), mark.end(), [](std::byte b) {
            return b == std::byte{kDirtyMagic};
        });
        w.dirty.set(k, dirty);
    }
    off_ = 0;
    phase_ = Phase::Write;

    if (vfs.file_exists(w.ram_path.view())) {
        auto in = vfs.open(w.ram_path.view(), svc::OpenMode::ReadWhole);
        const auto size = in ? (*in)->size() : Ex<svc::FileSize>{std::unexpected(in.error())};
        if (size && size->v == total_) {
            w.in = std::move(*in);
            phase_ = Phase::Compare;
        }
    }
    return {};
}

Ex<void> RamSaveJob::gather_(ICoreWindow& window, std::uint64_t off, std::size_t n) {
    Work& w = *work_;
    Ex<void> got{};
    w.map.for_each_ram_run(off, n, [&](std::uint32_t flat, std::size_t at, std::size_t len) {
        if (!got)
            return;
        auto r = window.read(flat, std::span(w.image).subspan(at, len));
        if (!r)
            got = std::unexpected(r.error());
    });
    return got;
}

Ex<void> RamSaveJob::compare_step_(ICoreWindow& window) {
    Work& w = *work_;
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(kStepBytes, total_ - off_));
    if (auto r = gather_(window, off_, n); !r)
        return r;
    const bool same = read_exact(*w.in, off_, std::span(w.file).first(n))
        && std::memcmp(w.image.data(), w.file.data(), n) == 0;
    if (!same) {
        w.in.reset();
        off_ = 0;
        phase_ = Phase::Write;
        return {};
    }
    off_ += n;
    if (off_ < total_)
        return {};
    w.in.reset();
    matched_ = true;
    phase_ = Phase::Clear;
    return {};
}

Ex<void> RamSaveJob::write_step_(ICoreWindow& window, const svc::Vfs& vfs) {
    Work& w = *work_;
    if (!w.out) {
        const std::string_view path = w.ram_path.view();
        const std::size_t slash = path.rfind('/');
        if (slash != std::string_view::npos) {
            if (auto d = vfs.ensure_dir(path.substr(0, slash)); !d)
                return d;
        }
        auto out = vfs.open(path, svc::OpenMode::Sync);
        if (!out)
            return std::unexpected(out.error());
        w.out = std::move(*out);
    }
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(kStepBytes, total_ - off_));
    if (auto r = gather_(window, off_, n); !r)
        return r;
    if (auto r = write_all(*w.out, off_, std::span(w.image).first(n)); !r)
        return r;
    off_ += n;
    if (off_ < total_)
        return {};
    w.out.reset();
    wrote_ = true;
    phase_ = Phase::Clear;
    return {};
}

Ex<void> RamSaveJob::clear_(ICoreWindow& window) {
    const std::array<std::byte, kDirtyMarkerBytes> zero{};
    for (std::uint32_t k = 0; k < kFlatBlocks; ++k) {
        if (!work_->dirty.test(k))
            continue;
        if (auto r = window.write(marker_offset(k), zero); !r)
            return std::unexpected(r.error());
        ++cleared_;
    }
    phase_ = Phase::Done;
    return {};
}

}
