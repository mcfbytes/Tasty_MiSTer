// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "hal/axi.h"
#include "hal/ocram_control_page.h"
#include "hal/ocram_hello_state.h"
#include "hal/ocram_ring_desc.h"
#include "hal/ocram_ring_lend.h"
#include "hal/ocram_types.h"
#include "hal/phys_region.h"
#include "infra/byte_offset.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "os/mmio_region.h"

namespace mister::hal::ocram {

class Window {
    TASTY_SEAT_RESIDENT(Main);

public:
    Window() = default;

    Window(Window&& o) noexcept
        : pages_(std::move(o.pages_)),
          base_(std::exchange(o.base_, nullptr)),
          region_end_(o.region_end_) {}

    Window& operator=(Window&& o) noexcept {
        if (this != &o) {
            pages_ = std::move(o.pages_);
            base_ = std::exchange(o.base_, nullptr);
            region_end_ = o.region_end_;
        }
        return *this;
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window() = default;

    [[nodiscard]] static Ex<Window> map(PhysRegion region) noexcept;

    [[nodiscard]] static Window over(std::span<std::uint32_t> words) noexcept;

    [[nodiscard]] bool mapped() const noexcept { return base_ != nullptr; }

    [[nodiscard]] ControlPage page() const noexcept { return ControlPage{base_}; }

    [[nodiscard]] RingLend lend_ring(RingDesc desc, Nonce nonce) const noexcept {
        if (base_ == nullptr || !desc.fits_in(region_end_))
            return RingLend{};
        return RingLend(base_, desc, nonce);
    }

private:
    Window(os::MmioRegion pages, std::size_t lead, infra::ByteOffset region_end) noexcept
        : pages_(std::move(pages)),
          base_(static_cast<volatile std::byte*>(detail::regs_advance(pages_->base(), lead))),
          region_end_(region_end) {}

    std::optional<os::MmioRegion> pages_;
    volatile std::byte* base_ = nullptr;
    infra::ByteOffset region_end_{};
};

}
