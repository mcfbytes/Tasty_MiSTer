// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_window.h"

#include <cstring>
#include <utility>

#include "hal/fabric_region.h"

namespace mister::app {

template <class Owner>
Ex<LoadWindow<Owner>> LoadWindow<Owner>::open(
    ILoadWindowMap<Owner>& map,
    const hal::PhysRegion& aperture,
    os::PhysAddr addr,
    std::uint64_t len
) {
    TASTY_SEAT_BODY(SeatOwner);
    auto region = hal::resolve_load_region(aperture, addr, len);
    if (!region)
        return std::unexpected(region.error());
    auto mem = map.map(*region);
    if (!mem)
        return std::unexpected(mem.error());
    return LoadWindow(std::move(*mem));
}

template <class Owner>
Ex<std::size_t> LoadWindow<Owner>::write(std::size_t off, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(SeatOwner);
    return mem_.write_at(off, src);
}

template <class Owner>
Ex<std::size_t> LoadWindow<Owner>::write_lanes(
    std::size_t off,
    std::span<const std::byte> src,
    std::size_t stride
)
    requires std::same_as<Owner, seat_owner::Main>
{
    TASTY_SEAT_BODY(SeatOwner);
    constexpr std::size_t kUnit = 2;
    const std::size_t units = src.size() / kUnit;
    if (stride == 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (units == 0)
        return std::size_t{0};
    const std::size_t len = kUnit * stride * (units - 1) + kUnit;
    const std::size_t size = mem_.region().len;
    if (off > size || len > size - off) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(off)}
        );
    }
    const std::span<std::byte> dst = mem_.view(off, len);
    for (std::size_t i = 0; i < units; ++i)
        std::memcpy(dst.data() + i * kUnit * stride, src.data() + i * kUnit, kUnit);
    mem_.publish();
    return units * kUnit;
}

template <class Owner>
Ex<std::size_t> LoadWindow<Owner>::read(std::size_t off, std::span<std::byte> dst) const {
    TASTY_SEAT_BODY(SeatOwner);
    return mem_.read_at(off, dst);
}

template <class Owner>
void LoadWindow<Owner>::publish() noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    mem_.publish();
}

template <class Owner>
std::size_t LoadWindow<Owner>::size() const noexcept {
    TASTY_SEAT_BODY(SeatOwner);
    return mem_.region().len;
}

template <class Owner>
void LoadWindow<Owner>::handoff() noexcept
    requires std::same_as<Owner, seat_owner::Main>
{
    TASTY_SEAT_BODY(SeatOwner);
    mem_.handoff();
}

template class LoadWindow<seat_owner::Main>;
template class LoadWindow<seat_owner::Io>;

}
