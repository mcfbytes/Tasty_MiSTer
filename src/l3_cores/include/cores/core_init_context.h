// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"

namespace mister::hal {
class ISpiTransport;
}

namespace mister::proto {
class CoreSession;
}

namespace mister::cores {

class CoreInitHost;
class Core;

struct CoreInitContext {
    hal::ISpiTransport& link;
    proto::CoreSession& session;
    CoreInitHost& host;
    infra::OptRef<Core> core{};
};

}
