// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "cores/atari8/atari8_disks.h"
#include "cores/atari8/atari8_machine.h"
#include "cores/atari8/atari8_sequencer.h"
#include "cores/atari8/atari8_status.h"
#include "cores/atari8/cart_slot.h"
#include "cores/atari8/cart_write_back.h"
#include "cores/atari8/cas_feed.h"
#include "cores/atari8/sio_slot.h"
#include "cores/mailbox_servant.h"
#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_select.h"
#include "proto/user_io/poll_frame.h"
#include "svc/file.h"

namespace mister::cores::atari8 {

class Atari8Servant final : public IMailboxServant {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    Atari8Servant() noexcept : sequencer_(Atari8Machine::Console) {}

    explicit Atari8Servant(Atari8Disks::Wiring disks) noexcept
        : sequencer_(Atari8Machine::Computer),
          disks_(std::in_place, disks) {}

    [[nodiscard]] proto::MailboxAct serve(
        proto::MailboxSelectIndex select,
        const proto::user_io::PollFrame& frame,
        ActBytes bytes,
        BulkBytes bulk
    ) noexcept override;
    [[nodiscard]] proto::MailboxAct settle(
        const proto::MailboxResult& result,
        ActBytes bytes,
        BulkBytes bulk
    ) noexcept override;
    void begin_core() noexcept override;

    void rebind(std::string_view stem, bool present) noexcept override;

    void refill_one() noexcept override { TASTY_SEAT_BODY(Atari8Servant); }

    [[nodiscard]] bool rest() const noexcept override {
        TASTY_SEAT_BODY(Atari8Servant);
        return true;
    }

    void release() noexcept override { TASTY_SEAT_BODY(Atari8Servant); }

    void mount(SioSlot slot, std::string_view path);

    [[nodiscard]] const Atari8Sequencer& sequencer() const noexcept { return sequencer_; }

    [[nodiscard]] const CartWriteBack& carts() const noexcept { return carts_; }

    [[nodiscard]] const CasFeed& tape() const noexcept { return tape_; }

    void hold_cart(CartSlot slot, CartWriteBack::Held held) noexcept;
    void drop_cart(CartSlot slot) noexcept;

    void mount_tape(std::unique_ptr<svc::IFile> file, bool boot) noexcept;

private:
    enum class Part : std::uint8_t { Sequencer, Disks, Carts, Tape };

    [[nodiscard]] proto::MailboxAct compose_(ActBytes bytes) noexcept;
    [[nodiscard]] proto::MailboxAct sent_(Part part, const proto::MailboxAct& act) noexcept;

    [[nodiscard]] proto::MailboxAct carts_or_tape_(
        ActBytes bytes,
        proto::MailboxAct::NotBefore now
    ) noexcept;

    Atari8Sequencer sequencer_;
    std::optional<Atari8Disks> disks_{};
    CartWriteBack carts_{};
    CasFeed tape_{};
    Atari8Status status_{};
    std::optional<Part> in_flight_{};
    bool carts_turn_ = false;
};

}
