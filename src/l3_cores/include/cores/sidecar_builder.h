// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "cores/sidecar_ask.h"
#include "cores/sidecar_plan.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

class ISidecarBuilder {
public:
    virtual ~ISidecarBuilder() = default;

    [[nodiscard]] virtual SidecarPlan plan(const SidecarAsk& ask, const svc::Vfs& vfs) = 0;

protected:
    ISidecarBuilder() = default;
    ISidecarBuilder(const ISidecarBuilder&) = default;
    ISidecarBuilder& operator=(const ISidecarBuilder&) = default;
};

using MakeSidecar = std::unique_ptr<ISidecarBuilder> (*)();

}
