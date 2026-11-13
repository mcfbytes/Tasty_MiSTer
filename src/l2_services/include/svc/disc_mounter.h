// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "infra/opt_ref.h"

namespace mister::svc {

struct CuePolicy;
class IDiscRules;

enum class DiscOp : std::uint8_t { Mount, Unmount, Release };

enum class DiscMountState : std::uint8_t { Idle, Pending, Mounted, Failed };

enum class DiscArm : std::uint8_t { Pending, Mounted, Failed, Declined };

class IDiscMounter {
public:
    virtual ~IDiscMounter() = default;
    IDiscMounter(const IDiscMounter&) = delete;
    IDiscMounter& operator=(const IDiscMounter&) = delete;
    IDiscMounter(IDiscMounter&&) = delete;
    IDiscMounter& operator=(IDiscMounter&&) = delete;

    [[nodiscard]] bool apply(
        DiscOp op,
        infra::OptRef<const CuePolicy> policy,
        std::string_view path,
        infra::OptRef<const IDiscRules> rules = {}
    ) noexcept {
        switch (op) {
            case DiscOp::Mount:
                return do_mount(policy, path, rules);
            case DiscOp::Unmount:
                return do_unmount(policy);
            case DiscOp::Release:
                do_release();
                return false;
        }
        return false;
    }

protected:
    IDiscMounter() = default;

private:
    [[nodiscard]] virtual bool do_mount(
        infra::OptRef<const CuePolicy> policy,
        std::string_view path,
        infra::OptRef<const IDiscRules> rules
    ) noexcept = 0;
    [[nodiscard]] virtual bool do_unmount(infra::OptRef<const CuePolicy> policy) noexcept = 0;

    virtual void do_release() noexcept = 0;
};

}
