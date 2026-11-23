// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mirror_fill.h"

#include <utility>

#include "infra/prelude.h"

namespace mister::app {

MirrorFill::MirrorFill(
    std::unique_ptr<svc::IMirrorSource> source,
    LoadWindow<seat_owner::Main> window
) noexcept
    : source_(std::move(source)),
      window_(std::move(window)) {}

Ex<MirrorFill> MirrorFill::open(
    std::unique_ptr<svc::IMirrorSource> source,
    const cores::DiskMirror& mirror,
    std::span<const cores::CoreWindowDecl> windows,
    const hal::FpgaAperture& aperture,
    ILoadWindowMap<seat_owner::Main>& map
) {
    TASTY_SEAT_BODY(MirrorFill);
    if (source == nullptr || !mirror.fits(windows)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const auto fabric = mirror.at.address_in(windows, aperture.region.phys);
    if (!fabric)
        return std::unexpected(fabric.error());
    const auto phys = aperture.physical_of(*fabric);
    if (!phys)
        return std::unexpected(phys.error());
    auto window = LoadWindow<seat_owner::Main>::open(
        map,
        aperture,
        *phys,
        mirror.room.file_word<std::size_t>()
    );
    if (!window)
        return std::unexpected(window.error());
    return MirrorFill{std::move(source), std::move(*window)};
}

Ex<bool> MirrorFill::step() {
    TASTY_SEAT_BODY(MirrorFill);
    if (handed_)
        return true;
    auto next = source_->next();
    if (!next)
        return std::unexpected(next.error());
    if (!next->has_value()) {
        window_.handoff();
        handed_ = true;
        return true;
    }
    const svc::MirrorPiece& piece = **next;
    const auto wrote = window_.write(
        (piece.at - proto::kFileStart).file_word<std::size_t>(),
        piece.bytes
    );
    if (!wrote)
        return std::unexpected(wrote.error());
    if (*wrote != piece.bytes.size()) {
        return std::unexpected(Error{Errc::short_write, ERR_SITE(), 0});
    }
    tally_.pieces.add();
    tally_.bytes = tally_.bytes + proto::FileSize::of(piece.bytes);
    return false;
}

}
