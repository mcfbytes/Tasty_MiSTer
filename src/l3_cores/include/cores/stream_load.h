// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/stream_load_end.h"
#include "infra/error.h"
#include "proto/session_params.h"
#include "proto/types.h"

namespace mister::cores {

using proto::IoIndex;

class IStreamLoad {
public:
    [[nodiscard]] virtual Ex<proto::SessionParams> stream_opening(IoIndex index) = 0;

    virtual void stream_closed(const StreamLoadEnd& end) noexcept = 0;

protected:
    IStreamLoad() = default;
    ~IStreamLoad() = default;
    IStreamLoad(const IStreamLoad&) = default;
    IStreamLoad& operator=(const IStreamLoad&) = default;
};

}  // namespace mister::cores
