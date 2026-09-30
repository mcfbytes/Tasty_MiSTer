// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class ISectorKick {
public:
    virtual ~ISectorKick() = default;

    virtual void service_kick() noexcept = 0;

protected:
    ISectorKick() = default;
    ISectorKick(const ISectorKick&) = default;
    ISectorKick& operator=(const ISectorKick&) = default;
};

}  // namespace mister::cores
