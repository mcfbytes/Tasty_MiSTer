// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {

class IFioTenant {
public:
    virtual ~IFioTenant() = default;
    IFioTenant() = default;
    IFioTenant(const IFioTenant&) = delete;
    IFioTenant& operator=(const IFioTenant&) = delete;

    [[nodiscard]] virtual bool holds_fio() const noexcept = 0;

    virtual void yield_to_reset() noexcept = 0;

protected:
    IFioTenant(IFioTenant&&) = default;
    IFioTenant& operator=(IFioTenant&&) = default;
};

}
