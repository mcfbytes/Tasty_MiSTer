// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/atari8/atari8_status.h"
#include "cores/atari8/pbi_mailbox.h"
#include "cores/atari8/sio_drives.h"
#include "cores/atari8/sio_engine.h"
#include "cores/atari8/sio_slot.h"
#include "cores/atari8/sio_terms.h"
#include "cores/atari8/xex_feed.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_result.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::atari8 {

class Atari8Disks {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;

    struct Wiring {
        const svc::Vfs& vfs;
        const os::IClock& clock;
    };

    enum class ColdBoot : std::uint8_t { Plain, Held };

    explicit Atari8Disks(Wiring w) noexcept : vfs_(w.vfs), clock_(w.clock) {}

    void begin_core() noexcept;

    void reset() noexcept;

    void warm_booted() noexcept;

    void mount(SioSlot slot, std::string_view path, Atari8Status status);
    void on_rx(SioRxStatus status) noexcept;

    [[nodiscard]] std::optional<ColdBoot> take_cold_boot() noexcept;

    [[nodiscard]] proto::MailboxAct next(ActBytes bytes, Atari8Status status);

    [[nodiscard]] bool settled(
        const proto::MailboxResult& result,
        ActBytes bytes,
        Atari8Status status
    );

private:
    enum class Part : std::uint8_t { Sio, Pbi, Xex };

    [[nodiscard]] proto::MailboxAct standing_(ActBytes bytes, const SioTerms& t);
    [[nodiscard]] proto::MailboxAct sent_(Part part, const proto::MailboxAct& act) noexcept;
    [[nodiscard]] SioTerms terms_(Atari8Status status) noexcept;

    const svc::Vfs& vfs_;
    const os::IClock& clock_;
    XexFeed xex_{};
    SioEngine sio_{};
    PbiMailbox pbi_{};
    SioDrives drives_{};
    std::optional<Part> out_{};
    bool pbi_next_ = true;
};

}
