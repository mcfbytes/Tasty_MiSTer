// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "cores/atari8/atari8_fill.h"
#include "cores/atari8/atari8_machine.h"
#include "cores/atari8/atari8_status.h"
#include "cores/atari8/atari8_wire.h"
#include "cores/atari8/cart_selects.h"
#include "cores/atari8/reboot_phase.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"

namespace mister::cores::atari8 {

class Atari8Sequencer {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;

    explicit Atari8Sequencer(Atari8Machine machine) noexcept : machine_(machine) {}

    void begin_core() noexcept;

    void order_reset() noexcept;

    void order_cold_boot(bool hold) noexcept;

    void release_hold() noexcept;
    [[nodiscard]] bool held() const noexcept;

    [[nodiscard]] bool take_warm_boot() noexcept;

    void on_status(Atari8Status status) noexcept;

    void on_carts(CartSelects carts) noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes) noexcept;

    void settled(const proto::MailboxResult& result) noexcept;

    [[nodiscard]] RebootPhase phase() const noexcept { return phase_; }

private:
    enum class Boot : std::uint8_t { Warm, Cold };
    enum class Half : std::uint8_t { Assert, Release, Whole };

    [[nodiscard]] bool asserts_() const noexcept { return half_ != Half::Release; }

    [[nodiscard]] bool releases_() const noexcept { return half_ != Half::Assert; }

    void begin_(Boot boot, Half half) noexcept;
    void read_edges_() noexcept;
    void advance_(const proto::MailboxResult& result) noexcept;
    void add_pause_(proto::MailboxRun& run, wire::Pause level) noexcept;
    [[nodiscard]] proto::MailboxAct reset_head_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct head_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct probe_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct line_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct block_(ActBytes bytes) const noexcept;

    Atari8Machine machine_;
    RebootPhase phase_ = RebootPhase::Rest;
    Boot boot_ = Boot::Warm;
    Half half_ = Half::Whole;

    wire::WriteRegister line_reg_ = wire::WriteRegister::Reset;
    std::optional<Atari8Status> status_{};
    std::optional<wire::Pause> pause_{};
    bool soft_held_ = false;
    bool cold_held_ = false;
    bool hold_ = false;
    bool warm_began_ = false;
    CartSelects carts_{};
    Atari8Fill fill_{};
};

}
