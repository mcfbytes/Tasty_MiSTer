// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::svc {

class IIoCoworker {
public:
    virtual ~IIoCoworker() = default;
    IIoCoworker(const IIoCoworker&) = delete;
    IIoCoworker& operator=(const IIoCoworker&) = delete;
    IIoCoworker(IIoCoworker&&) = delete;
    IIoCoworker& operator=(IIoCoworker&&) = delete;

    virtual void serve() noexcept = 0;
    [[nodiscard]] virtual bool idle() const noexcept = 0;

    virtual void on_pause() noexcept {}
    virtual void on_resume() noexcept {}

protected:
    IIoCoworker() = default;
};

}  // namespace mister::svc
