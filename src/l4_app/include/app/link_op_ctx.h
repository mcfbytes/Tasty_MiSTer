// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class LinkTxChannel;
struct EncoderRoles;
}  // namespace mister::app
namespace mister::proto {
class IResetFence;
}

namespace mister::app {

struct LinkOpCtx {
    LinkTxChannel* inbox = nullptr;
    bool windows_closed = false;
    bool live = false;

    proto::IResetFence* fence = nullptr;

    const EncoderRoles* roles = nullptr;
};

}  // namespace mister::app
