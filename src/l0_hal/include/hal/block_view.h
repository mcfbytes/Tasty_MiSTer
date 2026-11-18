// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_access.h"

namespace mister::hal {

namespace reg {
class Block;
}

template <class Layout>
class BlockView {
public:
    using Reg = typename Layout::Reg;
    static_assert(
        Layout::kSize >= sizeof(std::uint32_t) && Layout::kSize % sizeof(std::uint32_t) == 0,
        "a block holds whole dwords"
    );

    BlockView() = default;

    [[nodiscard]] bool bound() const noexcept { return base_ != nullptr; }

    [[nodiscard]] std::uint32_t read(Reg r) const noexcept
        requires(readable(Layout::kAccess))
    {
        return *word_(r);
    }

    void write(Reg r, std::uint32_t v) const noexcept
        requires(writable(Layout::kAccess))
    {
        *word_(r) = v;
    }

private:
    friend class reg::Block;

    explicit BlockView(std::byte* base) noexcept : base_(base) {}

    [[nodiscard]] auto word_(Reg r) const noexcept {
        return reinterpret_cast<volatile std::uint32_t*>(base_ + static_cast<std::uint32_t>(r));
    }

    std::byte* base_ = nullptr;
};

}
