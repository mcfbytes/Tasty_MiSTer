// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input_build.h"
#include "app/input_decode.h"
#include "app/input_emit.h"
#include "app/input_sample.h"
#include "app/input_wire.h"
#include "infra/error.h"
#include "os/clock.h"
#include "hal/spi_transport.h"
#include "proto/link_router.h"
#include "svc/input_service.h"
#include "svc/vfs.h"
#include "infra/seat.h"

namespace mister::app {

class InputPipeline {
    TASTY_SEAT_EXEMPT(boot);

public:
    InputPipeline(const svc::Vfs& vfs, hal::ISpiTransport& link, os::IClock& clock,
                  proto::ILinkRouter& router) noexcept;

    InputPipeline(const InputPipeline&) = delete;
    InputPipeline& operator=(const InputPipeline&) = delete;

    Ex<void> open();

    InputWire& wire() noexcept { return wire_; }
    const InputWire& wire() const noexcept { return wire_; }
    InputBuild& build() noexcept { return build_; }
    const InputBuild& build() const noexcept { return build_; }
    InputDecode& decode() noexcept { return decode_; }
    const InputDecode& decode() const noexcept { return decode_; }
    InputEmit& emit() noexcept { return emit_; }
    const InputEmit& emit() const noexcept { return emit_; }

    InputSample sample() const noexcept { return InputSample{&decode_, &build_, &emit_, &wire_}; }

private:
    InputWire wire_{};
    InputDecode decode_;
    InputBuild build_;
    InputEmit emit_;
};

struct InputStats {
    std::uint32_t rounds = 0;
    std::uint32_t rt_rounds = 0;
    std::uint32_t events = 0;
    std::uint32_t keys_published = 0;
    std::uint32_t keys_emitted = 0;
    std::uint32_t mouse_packets = 0;
    std::uint32_t joy_transactions = 0;
    std::uint32_t kicks = 0;
    std::uint32_t enumerate_failures = 0;
    std::uint32_t rt_errors = 0;
    std::uint32_t fd_evictions = 0;
    std::uint32_t edge_resets = 0;

    std::uint32_t devices = 0;
    std::uint32_t registered = 0;
    std::uint32_t mapped = 0;
    std::uint32_t slotted = 0;
    std::uint32_t gates = 0;
    std::uint32_t mask_changes = 0;
    std::uint32_t joy_seen = 0;
    std::uint32_t joy_players = 0;
    std::uint32_t captures = 0;
    std::uint32_t axis_edges = 0;
    std::uint32_t mouse_remainder = 0;
    std::uint32_t key_overflows = 0;
    std::uint32_t quirk_drops = 0;
    std::uint32_t rejected = 0;
    std::uint32_t ui_keys = 0;
    std::uint32_t ui_keys_dropped = 0;

    std::uint32_t slot_live = 0;
    std::uint32_t slot_ghosts = 0;
    std::uint32_t slot_hash[svc::kMaxPlayers]{};

    std::uint32_t keys_dropped = 0;
    std::uint32_t key_sweeps = 0;
    std::uint32_t keys_released = 0;
    std::uint32_t rebuilds_done = 0;
    std::uint32_t rebuilds_refused = 0;
    std::uint32_t button_samples = 0;
    std::uint32_t button_actions = 0;
};

InputStats collect_input_stats(const InputSample& s) noexcept;

}  // namespace mister::app
