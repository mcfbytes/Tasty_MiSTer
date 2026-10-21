// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"

namespace mister::app {

class ILinkTxReader;
struct EncoderRoles;
}

namespace mister::proto {
class IResetFence;
}

namespace mister::cores {
class Core;
}

namespace mister::app {

struct LinkOpCtx {

    const ILinkTxReader& inbox;
    bool windows_closed = false;
    bool live = false;

    bool starting = false;

    proto::IResetFence& fence;

    infra::OptRef<const EncoderRoles> roles{};

    infra::OptRef<cores::Core> core{};
};

}
