// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "cores/input_sink_id.h"
#include "hal/types.h"
#include "os/clock.h"
#include "proto/mouse_delta.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::cores {

class IInputSink {
public:
    using Instant = os::RawMonotonicClock::time_point;

    virtual ~IInputSink() = default;

    virtual void bind(Instant now) noexcept = 0;

    virtual void key(svc::EvdevCode code, bool pressed, Instant now) noexcept = 0;

    virtual void mouse(
        proto::MouseDelta delta,
        proto::MouseButtons buttons,
        Instant now
    ) noexcept = 0;

    virtual void controller_word(hal::SpiWord word, Instant now) noexcept = 0;

    virtual void tick(Instant now) noexcept = 0;

    [[nodiscard]] virtual std::optional<Instant> deadline() const noexcept = 0;

    [[nodiscard]] virtual std::optional<proto::Ps2Frame> next_frame() noexcept = 0;

protected:
    IInputSink() = default;
    IInputSink(const IInputSink&) = default;
    IInputSink& operator=(const IInputSink&) = default;
};

[[nodiscard]] std::unique_ptr<IInputSink> make_input_sink(InputSinkId id);

}
