// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::app {

class LinkTxChannel;

struct LinkOpCtx {
    LinkTxChannel* inbox = nullptr;
    bool windows_closed = false;
    bool live = false;
};

}  // namespace mister::app
