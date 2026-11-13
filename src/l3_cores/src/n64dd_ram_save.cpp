// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_ram_save.h"

#include <algorithm>

#include "infra/endian.h"
#include "infra/path_parts.h"

namespace mister::cores::n64dd {
namespace {

[[nodiscard]] FlatOffset marker_offset(std::uint32_t k) noexcept {
    return FlatBlock::of_index(k).offset().advanced_by(kDirtyFlagOffset);
}

}

std::optional<svc::SlotPathText> ram_save_path(
    std::string_view disk_path,
    std::string_view core_dir
) noexcept {
    svc::SlotPathText out{};
    bool ok = out.assign("saves/");
    ok = ok && out.append(core_dir);
    ok = ok && out.append("/");
    ok = ok && out.append(infra::stem_any_sep(disk_path));
    ok = ok && out.append(".ram");
    if (!ok)
        return std::nullopt;
    return out;
}

const WindowJobBuild kRamSaveKind = &build_ram_save_job;

std::optional<RamSaveOrder> RamSaveOrder::of(
    hal::FabricAddr load_addr,
    std::string_view disk_path,
    const CoreDir& core_dir
) noexcept {
    if (load_addr == hal::FabricAddr{} || disk_path.empty())
        return std::nullopt;
    RamSaveOrder o{};
    if (!o.disk_path.assign(disk_path))
        return std::nullopt;
    o.load_addr = load_addr;
    o.core_dir = core_dir;
    return o;
}

IWindowJob* build_ram_save_job(JobArena& arena, const WindowJobOrder& order) noexcept {
    seat_assert<SeatTag::Io>(ERR_SITE(), "build_ram_save_job off T-IO");
    const auto o = order.as<RamSaveOrder>();
    if (!o)
        return nullptr;
    return arena.emplace<RamSaveJob>(o->load_addr, o->disk_path.view(), o->core_dir.view());
}

RamSaveJob::RamSaveJob(
    hal::FabricAddr load_addr,
    std::string_view disk_path,
    std::string_view core_dir
) noexcept
    : addr_(load_addr) {
    path_ok_ = disk_path_.assign(disk_path) && core_dir_.assign(core_dir);
}

hal::FabricAddr RamSaveJob::window_addr() const noexcept {
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
    total_ = proto::FileSize{w.map.ram_bytes()};

    if (total_.empty()) {
        phase_ = Phase::Done;
        return {};
    }
    const auto ram_path = ram_save_path(disk_path_.view(), core_dir_.view());
    if (!ram_path)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    w.ram_path = *ram_path;

    std::array<std::byte, kDirtyMarkerBytes> mark{};
    for (std::uint32_t k = 0; k < kFlatBlocks; ++k) {
        if (auto r = marker_offset(k).read_via(window, mark); !r)
            return r;
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
        if (size && *size == total_) {
            w.in = std::move(*in);
            phase_ = Phase::Compare;
        }
    }
    return {};
}

Ex<void> RamSaveJob::gather_(ICoreWindow& window, std::uint64_t off, std::size_t n) {
    Work& w = *work_;
    Ex<void> got{};
    w.map.for_each_ram_run(off, n, [&](FlatOffset flat, std::size_t at, std::size_t len) {
        if (!got)
            return;
        auto r = flat.read_via(window, std::span(w.image).subspan(at, len));
        if (!r)
            got = std::unexpected(r.error());
    });
    return got;
}

Ex<void> RamSaveJob::compare_step_(ICoreWindow& window) {
    Work& w = *work_;
    const auto remaining = total_.after_header(proto::FileSize{off_});
    const auto n = static_cast<std::size_t>(
        std::min<std::uint64_t>(kStepBytes, remaining.file_word<std::size_t>())
    );
    if (auto r = gather_(window, off_, n); !r)
        return r;
    const bool same = svc::read_exact(*w.in, proto::FileSize{off_}, std::span(w.file).first(n))
                          .has_value()
        && std::memcmp(w.image.data(), w.file.data(), n) == 0;
    if (!same) {
        w.in.reset();
        off_ = 0;
        phase_ = Phase::Write;
        return {};
    }
    off_ += n;
    if (proto::FileSize{off_} < total_)
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
        if (const auto dir = infra::dirname(path); dir) {
            if (auto d = vfs.ensure_dir(*dir); !d)
                return d;
        }
        auto out = vfs.open(path, svc::OpenMode::Sync);
        if (!out)
            return std::unexpected(out.error());
        w.out = std::move(*out);
    }
    const auto remaining = total_.after_header(proto::FileSize{off_});
    const auto n = static_cast<std::size_t>(
        std::min<std::uint64_t>(kStepBytes, remaining.file_word<std::size_t>())
    );
    if (auto r = gather_(window, off_, n); !r)
        return r;
    if (auto r = svc::write_all(*w.out, proto::FileSize{off_}, std::span(w.image).first(n)); !r) {
        return r;
    }
    off_ += n;
    if (proto::FileSize{off_} < total_)
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
        if (auto r = marker_offset(k).write_via(window, zero); !r)
            return r;
        ++cleared_;
    }
    phase_ = Phase::Done;
    return {};
}

}
