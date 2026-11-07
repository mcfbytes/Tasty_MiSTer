// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/phys_region.h"
#include "hal/reg_block.h"
#include "hal/register_window.h"
#include "hal/types.h"

namespace mister::hal {

class LwSysWindow {

public:
    [[nodiscard]] static Ex<LwSysWindow> discover(
        PhysRegion board_lw,
        LwOffset off,
        std::uint32_t size
    ) noexcept {
        if (auto e = bound_(board_lw.len, off, size); e)
            return std::unexpected(*e);
        return LwSysWindow(board_lw, {}, off, size);
    }

    [[nodiscard]] static Ex<LwSysWindow> discover_in(
        std::span<std::uint32_t> image,
        LwOffset off,
        std::uint32_t size
    ) noexcept {
        if (auto e = bound_(image.size_bytes(), off, size); e)
            return std::unexpected(*e);
        return LwSysWindow({}, image, off, size);
    }

    enum : std::uint32_t { kWordBytes = sizeof(std::uint32_t), kWordAlignMask = kWordBytes - 1 };

    [[nodiscard]] LwOffset offset() const noexcept { return off_; }

    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }

    template <class Layout>
    [[nodiscard]] Ex<RegisterWindow<Layout>> map(std::uint32_t rel) const {
        const LwOffset at_rel{rel};
        if (!at_rel.dword_aligned() || !at_rel.fits(size_, Layout::kSize)) {
            return std::unexpected(Error{Errc::aperture_range, ERR_SITE(), rel});
        }
        const LwOffset at = off_.plus(rel);
        if (image_.empty())
            return RegisterWindow<Layout>::map(lw_, at);
        const std::size_t words = (Layout::kSize + kWordAlignMask) / kWordBytes;
        return RegisterWindow<Layout>::borrow(
            reg::Block::over(image_.subspan(at.dword_index(), words))
        );
    }

private:
    LwSysWindow(
        PhysRegion lw,
        std::span<std::uint32_t> image,
        LwOffset off,
        std::uint32_t size
    ) noexcept
        : lw_(lw),
          image_(image),
          off_(off),
          size_(size) {}

    [[nodiscard]] static std::optional<Error> bound_(
        std::size_t board_len,
        LwOffset off,
        std::uint32_t size
    ) noexcept {
        if (board_len == 0)
            return Error{Errc::dt_missing, ERR_SITE(), off.v};
        if (size < sizeof(std::uint32_t) || !off.dword_aligned() || !off.fits(board_len, size)) {
            return Error{Errc::bad_format, ERR_SITE(), off.v};
        }
        return std::nullopt;
    }

    PhysRegion lw_{};
    std::span<std::uint32_t> image_{};
    LwOffset off_{};
    std::uint32_t size_ = 0;
};

}
