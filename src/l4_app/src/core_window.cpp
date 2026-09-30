// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_window.h"

#include <utility>

namespace mister::app {

CoreWindow::CoreWindow(hal::FpgaMemory window) noexcept : window_(std::move(window)) {}

Ex<hal::FpgaMemory> CoreWindow::map_row(const hal::FpgaAperture& aperture,
                                        const hal::FabricRegion& row) {

    const auto at = aperture.translate(row.offset);
    if (!at) return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    if (!hal::fabric_row_fits(row, aperture)) {
        return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), os::low32(*at)});
    }
    return hal::FpgaMemory::map(hal::PhysRegion{*at, row.len, row.name});
}

Ex<CoreWindow> CoreWindow::open_row(const hal::FpgaAperture& aperture,
                                    const hal::FabricRegion& row) {
    auto mapped = map_row(aperture, row);
    if (!mapped) return std::unexpected(mapped.error());
    return CoreWindow(std::move(*mapped));
}

Ex<std::size_t> CoreWindow::write(std::size_t off, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.write_at(off, src);
}

Ex<std::size_t> CoreWindow::read(std::size_t off, std::span<std::byte> dst) const {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.read_at(off, dst);
}

void CoreWindow::publish() noexcept {
    TASTY_SEAT_BODY(CoreWindow);
    window_.publish();
}

std::size_t CoreWindow::size() const noexcept {
    TASTY_SEAT_BODY(CoreWindow);
    return window_.region().len;
}

const hal::PhysRegion& CoreWindow::region() const noexcept { return window_.region(); }

}  // namespace mister::app
