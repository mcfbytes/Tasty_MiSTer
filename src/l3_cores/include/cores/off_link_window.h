// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/stream_ask.h"
#include "cores/stream_load.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/aux_value.h"
#include "proto/link/fio_open.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

class OffLinkWindow {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<OffLinkWindow> open(
        IStreamLoad& core,
        const StreamAsk& ask,
        StreamLoadKind kind,
        proto::AuxValue length = {}
    );

    OffLinkWindow(OffLinkWindow&&) noexcept = default;
    OffLinkWindow& operator=(OffLinkWindow&&) = delete;
    OffLinkWindow(const OffLinkWindow&) = delete;
    OffLinkWindow& operator=(const OffLinkWindow&) = delete;
    ~OffLinkWindow() = default;

    [[nodiscard]] const proto::link::FioOpen& params() const noexcept { return params_; }

    [[nodiscard]] StreamLoadKind kind() const noexcept { return kind_; }

    void close(infra::OptRef<IStreamLoad> live, proto::FileSize bytes, svc::Crc32 crc) noexcept;
    void cut(infra::OptRef<IStreamLoad> live) noexcept;

private:
    OffLinkWindow(
        proto::FioIndexByte index,
        StreamLoadKind kind,
        const proto::link::FioOpen& params
    ) noexcept
        : index_(index),
          kind_(kind),
          params_(params) {}

    proto::FioIndexByte index_;
    StreamLoadKind kind_;
    proto::link::FioOpen params_;
};

}
