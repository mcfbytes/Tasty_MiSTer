// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::infra::xthread {

class ICoworker {
public:
    virtual ~ICoworker() = default;
    ICoworker(const ICoworker&) = delete;
    ICoworker& operator=(const ICoworker&) = delete;
    ICoworker(ICoworker&&) = delete;
    ICoworker& operator=(ICoworker&&) = delete;

    virtual void serve() noexcept = 0;

    virtual void on_pause() noexcept {}

    virtual void on_resume() noexcept {}

    virtual void on_stop() noexcept {}

protected:
    ICoworker() = default;
};

}
