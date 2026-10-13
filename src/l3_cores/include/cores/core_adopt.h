// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::proto {
class CoreSession;
}

namespace mister::cores {

struct CoreAdopt {
    proto::CoreSession& session;

    bool declares_turbo = false;
};

}  // namespace mister::cores
