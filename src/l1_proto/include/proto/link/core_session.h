// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "hal/core_signals.h"
#include "hal/spi_transport.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "proto/reset_fence.h"
#include "proto/rtc_image.h"
#include "proto/session_terms.h"
#include "proto/status_bit.h"
#include "proto/status_edit.h"
#include "proto/link/frame_route.h"
#include "proto/link/status_register.h"
#include "proto/status_word.h"
#include "proto/types.h"
#include "infra/seat.h"

namespace mister::proto::link {

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

class CoreSession {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static CoreSession detached(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        IResetFence* fence = nullptr,
        infra::OptRef<FrameRoute> frames = {}
    ) {
        return CoreSession(link, signals, fence, frames);
    }

    [[nodiscard]] static CoreSession adopted(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        MemSizeCookie cookie,
        IResetFence* fence = nullptr,
        infra::OptRef<FrameRoute> frames = {}
    ) {
        return CoreSession(link, signals, cookie, fence, frames);
    }

    SessionPhase phase() const noexcept { return phase_; }

    [[nodiscard]] bool negotiating() const noexcept;

    [[nodiscard]] Ex<CoreType> accept_identity(const Ex<hal::CoreTypeByte>& id);

    [[nodiscard]] Ex<void> configure_after_identity();

    [[nodiscard]] Ex<bool> settle_configure();

    [[nodiscard]] bool configure_posted() const noexcept {
        return memsz_post_ != MemSizePost::None;
    }

    void abandon_configure() noexcept;

    [[nodiscard]] Ex<void> set_memory_size(MemSizeCookie cookie);

    [[nodiscard]] Ex<void> assert_reset();

    [[nodiscard]] Ex<void> accept_conf_str(const Ex<std::span<const std::uint8_t>>& body);

    [[nodiscard]] bool conf_str_window_open() const noexcept;

    [[nodiscard]] Ex<void> load_saved_config();

    [[nodiscard]] Ex<void> send_rtc_image(const RtcImage& image);

    [[nodiscard]] Ex<void> release_reset();

    void refuse() noexcept {
        abandon_configure();
        phase_ = SessionPhase::Failed;
    }

    CoreType type() const noexcept { return type_; }

    [[nodiscard]] const SessionTerms& terms() const noexcept { return terms_; }

    bool dual_sdram() const noexcept { return dual_sdram_; }

    const hal::CoreCapabilities& capabilities() const noexcept { return caps_; }

    const StatusRegister& status() const noexcept { return status_; }

    void stage(const StatusEdit& edit) noexcept;

    [[nodiscard]] Ex<void> apply(const StatusEdit& edit);

    void post(const StatusEdit& edit) noexcept;

    [[nodiscard]] std::uint32_t deferred_bursts() const noexcept { return deferred_bursts_; }

    [[nodiscard]] Ex<void> pulse(StatusBit bit);

    [[nodiscard]] Ex<void> adopt_from_core(const StatusWord& core);

    [[nodiscard]] Ex<void> flush_owed();

    static constexpr std::size_t kConfStrCap = 10240;

    void set_suppress_status_wire(bool on) noexcept { suppress_status_wire_ = on; }

    [[nodiscard]] bool suppress_status_wire() const noexcept { return suppress_status_wire_; }

    [[nodiscard]] Ex<void> flush_status();

private:
    enum class MemSizePost : std::uint8_t { None, Opcode, Cookie };

    CoreSession(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        IResetFence* fence,
        infra::OptRef<FrameRoute> frames
    )
        : link_(&link),
          signals_(&signals),
          frames_(frames) {
        status_.fence_with(fence);
    }

    CoreSession(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        MemSizeCookie cookie,
        IResetFence* fence,
        infra::OptRef<FrameRoute> frames
    )
        : link_(&link),
          signals_(&signals),
          frames_(frames),
          phase_(SessionPhase::Start),
          cookie_(cookie) {
        status_.fence_with(fence);
    }

    [[nodiscard]] Ex<void> fail(Error e);

    [[nodiscard]] Ex<void> send_rtc_frames_(ILinkFrame& frame, const RtcImage& image);
    [[nodiscard]] bool ladder_window_() const noexcept;

    hal::ISpiTransport* link_;
    hal::ICoreSignals* signals_;

    infra::OptRef<FrameRoute> frames_{};
    SessionPhase phase_ = SessionPhase::Detached;
    MemSizeCookie cookie_{};
    CoreType type_ = CoreType::Unknown;
    SessionTerms terms_{};
    bool dual_sdram_ = false;
    bool suppress_status_wire_ = false;
    MemSizePost memsz_post_ = MemSizePost::None;
    std::uint32_t deferred_bursts_ = 0;
    hal::CoreCapabilities caps_{};
    StatusRegister status_;
};

}
