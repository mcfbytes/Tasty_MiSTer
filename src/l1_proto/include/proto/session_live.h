// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {

class ISessionLive {
public:
    virtual ~ISessionLive() = default;

    [[nodiscard]] virtual bool session_live() const noexcept = 0;

protected:
    ISessionLive() = default;
    ISessionLive(const ISessionLive&) = default;
    ISessionLive& operator=(const ISessionLive&) = default;
};

}  // namespace mister::proto
