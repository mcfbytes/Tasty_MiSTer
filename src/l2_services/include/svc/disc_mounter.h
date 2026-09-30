// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace mister::svc {

struct CuePolicy;

enum class DiscOp : std::uint8_t { Mount, Unmount, Release };

enum class DiscMountState : std::uint8_t { Idle, Pending, Mounted, Failed };

class IDiscMounter {
public:
    virtual ~IDiscMounter() = default;
    IDiscMounter(const IDiscMounter&) = delete;
    IDiscMounter& operator=(const IDiscMounter&) = delete;
    IDiscMounter(IDiscMounter&&) = delete;
    IDiscMounter& operator=(IDiscMounter&&) = delete;

    [[nodiscard]] bool apply(DiscOp op, const CuePolicy* policy, std::string_view path) noexcept {
        switch (op) {
            case DiscOp::Mount:
                return mount(policy, path);
            case DiscOp::Unmount:
                return unmount(policy);
            case DiscOp::Release:
                release();
                return false;
        }
        return false;
    }

    [[nodiscard]] virtual bool mount(const CuePolicy* policy, std::string_view path) noexcept = 0;
    [[nodiscard]] virtual bool unmount(const CuePolicy* policy) noexcept = 0;

    virtual void release() noexcept = 0;

protected:
    IDiscMounter() = default;
};

}  // namespace mister::svc
