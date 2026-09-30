// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
    Core* core = nullptr;
};

}  // namespace mister::cores
