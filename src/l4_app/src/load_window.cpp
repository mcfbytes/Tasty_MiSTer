// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_window.h"

#include <cstring>
#include <utility>

#include "hal/fabric_region.h"

namespace mister::app {

Ex<LoadWindow> LoadWindow::open(ILoadWindowMap& map, const hal::PhysRegion& aperture,
                                os::PhysAddr addr, std::uint64_t len) {
    auto region = hal::resolve_load_region(aperture, addr, len);
    if (!region) return std::unexpected(region.error());
    auto mem = map.map(*region);
    if (!mem) return std::unexpected(mem.error());
    return LoadWindow(std::move(*mem));
}

Ex<std::size_t> LoadWindow::write(std::size_t off, std::span<const std::byte> src) {
    return mem_.write_at(off, src);
}

Ex<std::size_t> LoadWindow::write_lanes(std::size_t off, std::span<const std::byte> src,
                                        std::size_t stride) {
    constexpr std::size_t kUnit = 2;
    const std::size_t units = src.size() / kUnit;
    if (stride == 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (units == 0) return std::size_t{0};
    const std::size_t len = kUnit * stride * (units - 1) + kUnit;
    const std::size_t size = mem_.region().len;
    if (off > size || len > size - off) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)});
    }
    const std::span<std::byte> dst = mem_.view(off, len);
    for (std::size_t i = 0; i < units; ++i)
        std::memcpy(dst.data() + i * kUnit * stride, src.data() + i * kUnit, kUnit);
    mem_.publish();
    return units * kUnit;
}

Ex<std::size_t> LoadWindow::read(std::size_t off, std::span<std::byte> dst) const {
    return mem_.read_at(off, dst);
}

void LoadWindow::publish() noexcept { mem_.publish(); }

std::size_t LoadWindow::size() const noexcept { return mem_.region().len; }

void LoadWindow::handoff() noexcept { mem_.handoff(); }

}  // namespace mister::app
