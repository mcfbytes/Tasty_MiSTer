// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/nic_mapped_window.h"

#include <array>
#include <span>
#include <utility>

namespace mister::svc {

Ex<void> NicMappedWindow::open() {
    TASTY_SEAT_BODY(NicMappedWindow);
    auto mapped = hal::FpgaMemory::map(page_);
    if (!mapped)
        return std::unexpected(mapped.error());
    map_.emplace(std::move(*mapped));
    return {};
}

INicWindow& NicMappedWindow::window() noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    return *this;
}

void NicMappedWindow::close() noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    map_.reset();
}

std::byte NicMappedWindow::load(infra::ByteOffset at) const noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    std::array<std::byte, 1> b{};
    (void)map_->read_at(at.bytes(), b);
    return b[0];
}

void NicMappedWindow::store(infra::ByteOffset at, std::byte b) noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    const std::array<std::byte, 1> one{b};
    (void)map_->write_at(at.bytes(), one);
}

NicSlotWord NicMappedWindow::load_slot(infra::ByteOffset at) const noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    std::array<std::byte, NicSlotWord::kBytes> b{};
    (void)map_->read_at(at.bytes(), b);
    return NicSlotWord::load(b);
}

void NicMappedWindow::store_slot(infra::ByteOffset at, NicSlotWord w) noexcept {
    TASTY_SEAT_BODY(NicMappedWindow);
    std::array<std::byte, NicSlotWord::kBytes> b{};
    w.store(b);
    (void)map_->write_at(at.bytes(), b);
}

}
