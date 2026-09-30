// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "hal/core_signals.h"
#include "hal/spi_transport.h"
#include "proto/reset_fence.h"
#include "proto/status_register.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto {

enum class SessionPhase : std::uint8_t {
    Detached,
    Start,
    Identified,
    MemSized,
    HeldInReset,
    ConfStrRead,
    Running,
    Failed,
};

enum class CoreType : std::uint8_t {
    Unknown = 0x55,
    EightBit = 0xA4,
    SharpMz = 0xA7,
};

class CoreSession {
    TASTY_SEAT_RESIDENT(RT);

public:
    CoreSession(hal::ISpiTransport& link, hal::ICoreSignals& signals, IResetFence* fence = nullptr)
        : link_(&link), signals_(&signals) {
        status_.fence_with(fence);
    }

    CoreSession(hal::ISpiTransport& link, hal::ICoreSignals& signals, MemSizeCookie cookie,
                IResetFence* fence = nullptr)
        : link_(&link), signals_(&signals), phase_(SessionPhase::Start), cookie_(cookie) {
        status_.fence_with(fence);
    }

    SessionPhase phase() const noexcept { return phase_; }

    [[nodiscard]] bool negotiating() const noexcept;

    [[nodiscard]] Ex<CoreType> accept_identity(const Ex<hal::CoreIdentity>& id);

    [[nodiscard]] Ex<void> configure_after_identity();

    Ex<void> set_memory_size(MemSizeCookie cookie);

    Ex<void> assert_reset();

    [[nodiscard]] Ex<void> accept_conf_str(const Ex<std::span<const std::uint8_t>>& body);

    [[nodiscard]] bool conf_str_window_open() const noexcept;

    Ex<void> load_saved_config();
    Ex<void> send_rtc();

    [[nodiscard]] Ex<void> send_rtc_at(std::uint32_t unix_seconds);

    Ex<void> release_reset();

    void refuse() noexcept { phase_ = SessionPhase::Failed; }

    CoreType type() const noexcept { return type_; }
    bool dual_sdram() const noexcept { return dual_sdram_; }
    const hal::CoreCapabilities& capabilities() const noexcept { return caps_; }
    StatusRegister& status() noexcept { return status_; }

    static constexpr std::size_t kConfStrCap = 10240;

    void set_suppress_status_wire(bool on) noexcept { suppress_status_wire_ = on; }
    [[nodiscard]] bool suppress_status_wire() const noexcept { return suppress_status_wire_; }

    [[nodiscard]] Ex<void> flush_status();

private:
    Ex<void> fail(Error e);
    [[nodiscard]] bool ladder_window_() const noexcept;

    hal::ISpiTransport* link_;
    hal::ICoreSignals* signals_;
    SessionPhase phase_ = SessionPhase::Detached;
    MemSizeCookie cookie_{};
    CoreType type_ = CoreType::Unknown;
    bool dual_sdram_ = false;
    bool suppress_status_wire_ = false;
    hal::CoreCapabilities caps_{};
    StatusRegister status_;
};

}  // namespace mister::proto
