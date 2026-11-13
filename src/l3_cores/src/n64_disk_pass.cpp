// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_disk_pass.h"

#include <algorithm>

#include "cores/n64dd_ram_save.h"
#include "infra/byte_view.h"

namespace mister::cores::n64 {

Ex<std::unique_ptr<DiskPass>> DiskPass::open(
    const svc::Vfs& vfs,
    const LoadAsk& ask,
    std::string_view core_dir
) {
    auto file = vfs.open(ask.path, svc::OpenMode::Read);
    if (!file)
        return std::unexpected(file.error());
    auto d = std::unique_ptr<DiskPass>(new DiskPass(vfs));
    auto area = n64dd::read_system_area(**file);
    if (!area)
        return std::unexpected(area.error());
    d->area_ = *area;
    if (auto m = n64dd::DiskMap::build_into(d->map_, d->area_); !m)
        return std::unexpected(m.error());
    d->expander_.start(d->map_);
    if (!d->path_.assign(ask.path))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});

    if (const auto ram_path = n64dd::ram_save_path(ask.path, core_dir))
        d->ram_path_ = *ram_path;
    d->load_addr_ = ask.load_addr;
    return d;
}

Ex<void> DiskPass::place(std::span<const std::uint8_t> piece, ICoreWindow& window) {
    TASTY_SEAT_BODY(DiskPass);
    return expander_.feed(piece, window);
}

Ex<bool> DiskPass::conclude(ICoreWindow& window) {
    TASTY_SEAT_BODY(DiskPass);

    if (!expander_.complete())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return load_ram_step_(window);
}

Ex<bool> DiskPass::load_ram_step_(ICoreWindow& window) {
    const proto::FileSize total{map_.ram_bytes()};
    if (!ram_checked) {
        ram_checked = true;
        if (total.empty() || ram_path_.empty() || !vfs_.file_exists(ram_path_.view()))
            return false;
        auto f = vfs_.open(ram_path_.view(), svc::OpenMode::ReadWhole);
        if (!f)
            return false;
        auto size = (*f)->size();

        if (!size || *size != total)
            return false;
        ram_file_ = std::move(*f);
        ram_off = proto::FileSize{};
    }
    if (!ram_file_)
        return false;

    const proto::FileSize staged = proto::FileSize::of(std::span<std::uint8_t>(ram_buf));
    const std::span<std::uint8_t> piece = total.after_header(ram_off).min(staged).first_of(
        std::span<std::uint8_t>(ram_buf)
    );
    if (auto r = svc::read_exact(*ram_file_, ram_off, std::as_writable_bytes(piece)); !r) {
        ram_file_.reset();
        return false;
    }
    Ex<void> placed{};
    map_.for_each_ram_run(
        ram_off.file_word<std::uint64_t>(),
        piece.size(),
        [&](n64dd::FlatOffset flat, std::size_t at, std::size_t n) {
            if (!placed)
                return;
            auto w = flat.write_via(window, std::as_bytes(piece.subspan(at, n)));
            if (!w)
                placed = std::unexpected(w.error());
        }
    );
    if (!placed)
        return std::unexpected(placed.error());
    ram_off = ram_off.advance(proto::FileSize::of(piece));
    if (ram_off < total)
        return true;
    ram_loaded = total;
    ram_file_.reset();
    return false;
}

Ex<bool> DiskPass::finish(LoadFacts& slab, infra::OptRef<ICoreWindow>) {
    TASTY_SEAT_BODY(DiskPass);
    if (!expander_.complete())
        return false;

    slab = LoadFacts{};
    slab.kind = FactsKind::Disk;
    slab.load_addr = load_addr_;
    return true;
}

}
