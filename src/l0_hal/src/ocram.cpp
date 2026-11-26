// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/ocram.h"

#include <span>
#include <utility>

#include "hal/axi.h"
#include "hal/phys_region.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_main.h"
#include "infra/seat_owner_ui.h"

namespace mister::hal::ocram {

Ex<Window> Window::map(PhysRegion region) noexcept {

    const infra::ByteCount span = infra::ByteCount::of(region.len);
    if (span < control::kSpan || kMapSpan < span) {
        return std::unexpected(span.refusal(Errc::slot_range, ERR_SITE()));
    }
    auto mapped = detail::map_registers(region.phys, region.len);
    if (!mapped)
        return std::unexpected(mapped.error());
    return Window(std::move(mapped->pages), mapped->lead, infra::ByteOffset{region.len});
}

Window Window::over(std::span<std::uint32_t> words) noexcept {
    if (words.empty())
        return Window{};
    Window w;
    w.base_ = reinterpret_cast<volatile std::byte*>(words.data());
    w.region_end_ = infra::ByteOffset{words.size_bytes()};
    return w;
}

template class RingWriter<seat_owner::Ui>;
template class RingWriter<seat_owner::Io>;
template class RingWriter<seat_owner::Main>;
template class RingWriter<seat_owner::RT>;
template class RingReader<seat_owner::Io>;

}
