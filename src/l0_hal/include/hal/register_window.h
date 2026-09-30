// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "infra/error.h"
#include "hal/axi.h"
#include "os/mmio_region.h"
#include "hal/phys_region.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::hal {

template <class Layout>
class RegisterWindow {
    TASTY_SEAT_EXEMPT(const_shared);

public:
    using Reg = typename Layout::Reg;

    static Ex<RegisterWindow> map(PhysRegion lw_window, LwOffset lw_offset) {
        auto m = detail::map_lw_registers(lw_window, lw_offset, Layout::kSize);
        if (!m) return std::unexpected(m.error());
        return RegisterWindow(std::move(m->pages), m->lead);
    }

    static Ex<RegisterWindow> map_phys(os::PhysAddr phys) {
        auto m = detail::map_registers(phys, Layout::kSize);
        if (!m) return std::unexpected(m.error());
        return RegisterWindow(std::move(m->pages), m->lead);
    }

    static RegisterWindow borrow(volatile void* base, std::size_t len) {
        if (base == nullptr || len < Layout::kSize) {
            fatal(Error{Errc::mmap_failed, ERR_SITE(), static_cast<std::uint32_t>(len)},
                  "RegisterWindow::borrow");
        }
        return RegisterWindow(base);
    }

    RegisterWindow(RegisterWindow&& o) noexcept
        : pages_(std::move(o.pages_)), base_(std::exchange(o.base_, nullptr)) {
#ifndef NDEBUG
        depth_ = std::exchange(o.depth_, 0u);
#endif
    }
    RegisterWindow& operator=(RegisterWindow&& o) noexcept {
        if (this != &o) {
            pages_ = std::move(o.pages_);
            base_ = std::exchange(o.base_, nullptr);
#ifndef NDEBUG
            depth_ = std::exchange(o.depth_, 0u);
#endif
        }
        return *this;
    }
    RegisterWindow(const RegisterWindow&) = delete;
    RegisterWindow& operator=(const RegisterWindow&) = delete;
    ~RegisterWindow() = default;

    std::uint32_t read(Reg r) const { return detail::regs_read(base_, offset_of(r)); }
    void write(Reg r, std::uint32_t v) { detail::regs_write(base_, offset_of(r), v); }

    class Transaction {
    public:
        explicit Transaction(RegisterWindow& w) {
#ifndef NDEBUG
            depth_ = &w.depth_;
            if (*depth_ != 0) {
                fatal(Error{Errc::bridge_state, ERR_SITE(), *depth_},
                      "RegisterWindow: nested Transaction");
            }
            ++*depth_;
#else
            (void)w;
#endif
            detail::regs_fence();
        }
        ~Transaction() {
            detail::regs_fence();
#ifndef NDEBUG
            --*depth_;
#endif
        }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

#ifndef NDEBUG
    private:
        std::uint32_t* depth_ = nullptr;
#endif
    };

private:
    RegisterWindow() = default;
    RegisterWindow(os::MmioRegion pages, std::size_t lead)
        : pages_(std::move(pages)), base_(detail::regs_advance(pages_->base(), lead)) {}
    explicit RegisterWindow(volatile void* base) : base_(base) {}

    static_assert(Layout::kSize >= 4, "a register window holds at least one dword");

    std::size_t offset_of(Reg r) const {
        const auto off = static_cast<std::uint32_t>(r);
        if (base_ == nullptr) {
            fatal(Error{Errc::mmap_failed, ERR_SITE(), off}, "RegisterWindow: unmapped");
        }

        if ((off & 3u) != 0u || static_cast<std::size_t>(off) > Layout::kSize - 4u) {
            fatal(Error{Errc::slot_range, ERR_SITE(), off}, "RegisterWindow: offset");
        }
        return static_cast<std::size_t>(off);
    }

    std::optional<os::MmioRegion> pages_;
    volatile void* base_ = nullptr;
#ifndef NDEBUG
    std::uint32_t depth_ = 0;
#endif
};

}  // namespace mister::hal
