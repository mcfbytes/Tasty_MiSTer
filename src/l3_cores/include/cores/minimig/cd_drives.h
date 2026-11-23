// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <string_view>

#include "cores/mailbox_servant.h"
#include "cores/minimig/akiko_drive.h"
#include "cores/minimig/cd_disc.h"
#include "cores/minimig/cd_slot.h"
#include "cores/minimig/cd_status.h"
#include "cores/minimig/cr511_drive.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/monotonic_clock.h"
#include "svc/vfs.h"

namespace mister::cores::minimig {

class CdDrives final : public IMailboxServant {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    using Instant = proto::MailboxAct::NotBefore;

    static constexpr std::chrono::milliseconds kTickCap{5};

    static constexpr std::chrono::milliseconds kTickFloor{1};

    explicit CdDrives(const svc::Vfs& vfs) : clock_(own_clock_), akiko_(vfs) {}

    CdDrives(const svc::Vfs& vfs, const os::IClock& clock) : clock_(clock), akiko_(vfs) {}

    void set_active(CdSlot slot, bool on) noexcept;

    void bind(CdSlot slot, std::optional<CdDisc> disc, std::string_view path) noexcept;

    void reset() noexcept;

    void on_osd(bool shown) noexcept;

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

    void refill_one() noexcept override { TASTY_SEAT_BODY(CdDrives); }

    [[nodiscard]] bool rest() const noexcept override {
        TASTY_SEAT_BODY(CdDrives);
        return true;
    }

    void release() noexcept override { TASTY_SEAT_BODY(CdDrives); }

    [[nodiscard]] const AkikoDrive& akiko() const noexcept { return akiko_; }

    [[nodiscard]] const Cr511Drive& cdtv() const noexcept { return cdtv_; }

private:
    enum class Out : std::uint8_t { None, Akiko, Cdtv, Tick };

    void route_(proto::MailboxSelectIndex select, const proto::user_io::PollFrame& frame) noexcept;

    [[nodiscard]] proto::MailboxAct pump_(BulkBytes bulk) noexcept;
    [[nodiscard]] proto::MailboxAct tick_(Instant now) noexcept;

    os::MonotonicClock own_clock_{};
    const os::IClock& clock_;
    AkikoDrive akiko_;
    Cr511Drive cdtv_;
    Out out_ = Out::None;
    Out last_ = Out::Cdtv;

    bool status_fresh_ = false;
};

}
