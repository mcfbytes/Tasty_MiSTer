// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "infra/error.h"
#include "infra/prelude.h"
#include "svc/nic_grant.h"
#include "svc/nic_poll.h"

namespace mister::svc {

class INicPersonality {
public:
    virtual ~INicPersonality() = default;

    [[nodiscard]] virtual Ex<void> start() = 0;

    virtual void reset() noexcept = 0;

    [[nodiscard]] virtual NicPoll poll() noexcept = 0;

    virtual void stop() noexcept = 0;

protected:
    INicPersonality() = default;
    INicPersonality(const INicPersonality&) = default;
    INicPersonality& operator=(const INicPersonality&) = default;
};

using MakeNic = std::unique_ptr<INicPersonality> (*)(const NicGrant&);

}
