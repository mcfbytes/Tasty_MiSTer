// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_word.h"

namespace mister::hal {

enum class RegAccess : std::uint8_t { ReadOnly, ReadWrite };

template <class Layout, RegAccess Access = RegAccess::ReadWrite>
class IRegPort {
public:
    using Reg = typename Layout::Reg;

    [[nodiscard]] virtual RegWord read(Reg r) noexcept = 0;
    virtual void write(Reg r, RegWord w) noexcept = 0;

protected:
    IRegPort() = default;
    IRegPort(const IRegPort&) = default;
    IRegPort& operator=(const IRegPort&) = default;
    ~IRegPort() = default;
};

template <class Layout>
class IRegPort<Layout, RegAccess::ReadOnly> {
public:
    using Reg = typename Layout::Reg;

    [[nodiscard]] virtual RegWord read(Reg r) noexcept = 0;

protected:
    IRegPort() = default;
    IRegPort(const IRegPort&) = default;
    IRegPort& operator=(const IRegPort&) = default;
    ~IRegPort() = default;
};

}
