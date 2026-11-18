// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/reg_access.h"
#include "hal/reg_word.h"

namespace mister::hal {

template <class Layout, class = void>
class IRegPort;

template <class Layout>
    requires(readable(Layout::kAccess) && writable(Layout::kAccess))
class IRegPort<Layout, void> {
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
    requires(readable(Layout::kAccess) && !writable(Layout::kAccess))
class IRegPort<Layout, void> {
public:
    using Reg = typename Layout::Reg;

    [[nodiscard]] virtual RegWord read(Reg r) noexcept = 0;

protected:
    IRegPort() = default;
    IRegPort(const IRegPort&) = default;
    IRegPort& operator=(const IRegPort&) = default;
    ~IRegPort() = default;
};

template <class Layout>
    requires(!readable(Layout::kAccess) && writable(Layout::kAccess))
class IRegPort<Layout, void> {
public:
    using Reg = typename Layout::Reg;

    virtual void write(Reg r, RegWord w) noexcept = 0;

protected:
    IRegPort() = default;
    IRegPort(const IRegPort&) = default;
    IRegPort& operator=(const IRegPort&) = default;
    ~IRegPort() = default;
};

}
