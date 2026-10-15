// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"

namespace mister::app {

class ILinkTxReader;
struct EncoderRoles;
}  // namespace mister::app
namespace mister::proto {
class IResetFence;
}

namespace mister::app {

struct LinkOpCtx {

    infra::OptRef<const ILinkTxReader> inbox{};
    bool windows_closed = false;
    bool live = false;

    infra::OptRef<proto::IResetFence> fence{};

    infra::OptRef<const EncoderRoles> roles{};
};

}  // namespace mister::app
