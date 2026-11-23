// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "cores/atari8/atari8_address.h"
#include "cores/atari8/cart_flash.h"
#include "cores/atari8/cart_match.h"
#include "cores/atari8/cart_selects.h"
#include "cores/atari8/cart_slot.h"
#include "infra/endian.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_run.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::atari8 {

struct CarChecksum {
    TASTY_SEAT_EXEMPT(component);

    std::uint32_t v = 0;

    void add(std::span<const std::uint8_t> bytes) noexcept {
        for (const std::uint8_t b : bytes)
            v += b;
    }

    void store_be(std::span<std::byte, 4> out) const noexcept {
        infra::store_be<std::uint32_t>(out, v);
    }

    friend constexpr bool operator==(CarChecksum, CarChecksum) = default;
};

class CartWriteBack {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;
    using NotBefore = proto::MailboxAct::NotBefore;

    struct Held {
        std::unique_ptr<svc::IFile> file;
        CartMatch match{};
        bool writable = false;
    };

    enum class Phase : std::uint8_t { Idle, Poll, MoveRead, MoveWrite, MoveClear, Save };

    void begin_core() noexcept;

    void hold(CartSlot slot, Held held) noexcept;

    void drop(CartSlot slot) noexcept;

    [[nodiscard]] CartSelects selects() const noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes, NotBefore now) noexcept;
    void settled(const proto::MailboxResult& result, ActBytes bytes, NotBefore now) noexcept;

    [[nodiscard]] Phase phase() const noexcept { return phase_; }

    static constexpr std::chrono::milliseconds kPollPeriod{5};
    static constexpr std::chrono::milliseconds kAutosaveQuiet{2000};

private:
    struct Save {
        CartSlot slot = CartSlot::First;
        proto::FileOffset at{};
        proto::FileOffset end{};
        Atari8Address memory{};
        bool car = false;
        CarChecksum sum{};
    };

    [[nodiscard]] static proto::MailboxActLen piece_len(const Save& s) noexcept;
    [[nodiscard]] std::optional<Held>& held_(CartSlot slot) noexcept;
    [[nodiscard]] bool holds_any_() const noexcept;
    void read_flash_(CartFlash flash, NotBefore now) noexcept;
    [[nodiscard]] std::optional<Save> save_of_(CartSlot slot) noexcept;
    void saved_(std::span<const std::uint8_t> piece) noexcept;
    [[nodiscard]] proto::MailboxAct move_act_(ActBytes bytes) const noexcept;
    [[nodiscard]] proto::MailboxAct save_act_() const noexcept;

    std::optional<Held> first_{};
    std::optional<Held> second_{};
    Phase phase_ = Phase::Idle;
    NotBefore next_poll_{};

    bool dirty_ = false;
    bool dirty_second_ = false;
    NotBefore quiet_at_{};

    CartSlot moving_ = CartSlot::First;
    proto::FileSize moved_{};
    std::array<std::uint8_t, proto::kMailboxActBytes> piece_{};
    std::optional<Save> save_{};
};

}
