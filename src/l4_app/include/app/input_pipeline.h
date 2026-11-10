// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/input_build.h"
#include "app/input_decode.h"
#include "app/input_emit.h"
#include "app/input_sample.h"
#include "app/input_wire.h"
#include "os/clock.h"
#include "svc/input_service_counts.h"
#include "svc/input_service.h"
#include "svc/types.h"
#include "infra/seat.h"

namespace mister::app {

class InputPipeline {
    TASTY_SEAT_EXEMPT(boot);

public:
    InputPipeline(
        InputDecode::Fds fds,
        InputWire& wire,
        InputBuild& build,
        InputEmit& emit,
        svc::InputService service,
        os::IClock& clock,
        InputDecode::Wiring decode_wiring
    ) noexcept;

    InputPipeline(const InputPipeline&) = delete;
    InputPipeline& operator=(const InputPipeline&) = delete;

    InputWire& wire() noexcept { return wire_; }

    const InputWire& wire() const noexcept { return wire_; }

    InputBuild& build() noexcept { return build_; }

    const InputBuild& build() const noexcept { return build_; }

    InputDecode& decode() noexcept { return decode_; }

    const InputDecode& decode() const noexcept { return decode_; }

    InputEmit& emit() noexcept { return emit_; }

    const InputEmit& emit() const noexcept { return emit_; }

    svc::InputService& service() noexcept { return service_; }

    const svc::InputService& service() const noexcept { return service_; }

    void latch_cfg(std::span<const svc::DeadzoneRule> deadzones, bool rumble) noexcept;

    const InputSample& sample() const noexcept { return sample_; }

private:
    InputWire& wire_;
    InputBuild& build_;
    InputEmit& emit_;
    svc::InputService service_;
    InputDecode decode_;
    const InputSample sample_;
};

struct InputStats {
    std::uint32_t rounds = 0;
    std::uint32_t rt_rounds = 0;
    std::uint32_t events = 0;
    std::uint32_t keys_published = 0;
    std::uint32_t keys_emitted = 0;
    std::uint32_t mouse_packets = 0;
    std::uint32_t kicks = 0;
    std::uint32_t enumerate_failures = 0;
    std::uint32_t rt_errors = 0;
    std::uint32_t fd_evictions = 0;
    std::uint32_t edge_resets = 0;

    std::uint32_t devices = 0;
    std::uint32_t registered = 0;
    InputGateBits gates{};
    svc::JoyMask joy_seen{};
    svc::PlayerMask joy_players{};
    std::uint32_t captures = 0;

    svc::InputServiceCounts service{};
    std::uint32_t rejected = 0;
    std::uint32_t ui_keys = 0;
    std::uint32_t ui_keys_dropped = 0;

    std::uint32_t keys_dropped = 0;
    std::uint32_t key_sweeps = 0;
    std::uint32_t keys_released = 0;
    std::uint32_t rebuilds_done = 0;
    std::uint32_t rebuilds_refused = 0;
    std::uint32_t button_actions = 0;
};

InputStats collect_input_stats(const InputSample& s, const InputBuild::Counts& b) noexcept;

}
