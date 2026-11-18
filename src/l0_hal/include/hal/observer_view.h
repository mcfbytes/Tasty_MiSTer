// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "infra/error.h"
#include "infra/persist.h"
#include "infra/seat.h"
#include "hal/axi.h"
#include "hal/phys_region.h"
#include "hal/reg_block.h"
#include "hal/reg_word.h"
#include "os/mmio_region.h"
#include "os/types.h"

namespace mister::hal {

struct LiveViews {
    std::size_t v = 0;
    friend constexpr bool operator==(LiveViews, LiveViews) = default;
};

namespace detail {

inline std::atomic<std::size_t>& live_observer_views() noexcept {
    static std::atomic<std::size_t> count TASTY_PERSIST(proc, observer_view_live){0};
    return count;
}

struct NoLayout {
    enum class Reg : std::uint32_t {};
};

}

template <class Layout = detail::NoLayout>
class ObserverView {
    TASTY_SEAT_EXEMPT(component);

public:
    using Reg = typename Layout::Reg;

    [[nodiscard]] static Ex<ObserverView> open(os::PhysAddr phys) {
        static_assert(Layout::kSize >= 4, "an observer view holds at least one dword");
        auto pages = os::MmioRegion::map(
            phys,
            Layout::kSize,
            os::MmioRegion::Attr::Device,
            os::MmioRegion::Access::ReadOnly
        );
        if (!pages)
            return std::unexpected(pages.error());
        return ObserverView(std::move(*pages));
    }

    [[nodiscard]] static ObserverView borrow(std::span<std::uint32_t> words) {
        static_assert(Layout::kSize >= 4, "an observer view holds at least one dword");
        if (words.size_bytes() < Layout::kSize) {
            fatal(Error{Errc::mmap_failed, ERR_SITE(), 0}, "ObserverView::borrow");
        }
        return ObserverView(words.data());
    }

    ObserverView(ObserverView&& o) noexcept
        : pages_(std::move(o.pages_)),
          base_(std::exchange(o.base_, nullptr)),
          counted_(std::exchange(o.counted_, false)) {}

    ObserverView& operator=(ObserverView&& o) noexcept {
        if (this != &o) {
            close();
            pages_ = std::move(o.pages_);
            base_ = std::exchange(o.base_, nullptr);
            counted_ = std::exchange(o.counted_, false);
        }
        return *this;
    }

    ObserverView(const ObserverView&) = delete;
    ObserverView& operator=(const ObserverView&) = delete;

    ~ObserverView() { close(); }

    [[nodiscard]] RegWord read(Reg r) const {
        static_assert(Layout::kSize >= 4, "an observer view holds at least one dword");
        const auto off = static_cast<std::uint32_t>(r);
        if (base_ == nullptr) {
            fatal(Error{Errc::mmap_failed, ERR_SITE(), off}, "ObserverView: unmapped");
        }
        const std::size_t byte_off = off;
        if ((off & reg::Block::kWordAlignMask) != 0u
            || !span_fits(byte_off, sizeof(std::uint32_t), Layout::kSize)) {
            fatal(Error{Errc::slot_range, ERR_SITE(), off}, "ObserverView: offset");
        }
        return RegWord{detail::regs_read(base_, byte_off)};
    }

    [[nodiscard]] static LiveViews live() noexcept {
        return LiveViews{detail::live_observer_views().load(std::memory_order_relaxed)};
    }

private:
    explicit ObserverView(os::MmioRegion pages) : pages_(std::move(pages)), base_(pages_->base()) {
        detail::live_observer_views().fetch_add(1, std::memory_order_relaxed);
        counted_ = true;
    }

    explicit ObserverView(volatile void* base) noexcept : base_(base) {
        detail::live_observer_views().fetch_add(1, std::memory_order_relaxed);
        counted_ = true;
    }

    void close() noexcept {
        if (!counted_)
            return;
        counted_ = false;
        detail::live_observer_views().fetch_sub(1, std::memory_order_relaxed);
    }

    std::optional<os::MmioRegion> pages_;
    volatile void* base_;
    bool counted_ = false;
};

}
