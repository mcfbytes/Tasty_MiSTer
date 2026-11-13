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
#include "os/map_length.h"

namespace mister::hal {

class LwSysWindow {

public:
    [[nodiscard]] static Ex<LwSysWindow> discover(
        PhysRegion board_lw,
        LwOffset off,
        LwExtent size
    ) noexcept {
        if (auto e = bound_(os::MapLength{board_lw.len}, off, size); e)
            return std::unexpected(*e);
        return LwSysWindow(board_lw, {}, off, size);
    }

    [[nodiscard]] static Ex<LwSysWindow> discover_in(
        std::span<std::uint32_t> image,
        LwOffset off,
        LwExtent size
    ) noexcept {
        if (auto e = bound_(os::MapLength{image.size_bytes()}, off, size); e)
            return std::unexpected(*e);
        return LwSysWindow({}, image, off, size);
    }

    enum : std::uint32_t { kWordBytes = sizeof(std::uint32_t), kWordAlignMask = kWordBytes - 1 };

    [[nodiscard]] LwOffset offset() const noexcept { return off_; }

    [[nodiscard]] LwExtent size() const noexcept { return size_; }

    template <class Layout>
    [[nodiscard]] Ex<RegisterWindow<Layout>> map(LwOffset rel) const {
        if (!rel.dword_aligned() || !rel.fits(size_, LwExtent::of_layout<Layout>())) {
            return std::unexpected(rel.refused(Errc::aperture_range, ERR_SITE()));
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
    LwSysWindow(PhysRegion lw, std::span<std::uint32_t> image, LwOffset off, LwExtent size) noexcept
        : lw_(lw),
          image_(image),
          off_(off),
          size_(size) {}

    static constexpr LwExtent kMinExtent{kWordBytes};

    [[nodiscard]] static std::optional<Error> bound_(
        os::MapLength board_len,
        LwOffset off,
        LwExtent size
    ) noexcept {
        if (board_len.empty())
            return off.refused(Errc::dt_missing, ERR_SITE());
        const std::optional<LwExtent> board = LwExtent::of_length(board_len);
        if (!board || !size.covers(kMinExtent) || !off.dword_aligned() || !off.fits(*board, size)) {
            return off.refused(Errc::bad_format, ERR_SITE());
        }
        return std::nullopt;
    }

    PhysRegion lw_{};
    std::span<std::uint32_t> image_{};
    LwOffset off_{};
    LwExtent size_{};
};

}
