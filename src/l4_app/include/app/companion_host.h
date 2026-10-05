// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/companion_bind.h"
#include "cores/mailbox_servants.h"
#include "infra/counter.h"
#include "infra/inbox.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class CompanionHost {
    TASTY_SEAT_RESIDENT(Pcm);

public:
    static constexpr std::size_t kBindDepth = 4;
    using Binds = xthread::Inbox<CompanionBind, kBindDepth>;

    explicit CompanionHost(xthread::WakeFlag& servant_wake) noexcept : binds_(servant_wake) {}
    CompanionHost(const CompanionHost&) = delete;
    CompanionHost& operator=(const CompanionHost&) = delete;

    void install(cores::ServantSet set) noexcept;

    [[nodiscard]] Binds& binds() noexcept { return binds_; }

    void serve() noexcept;

    [[nodiscard]] cores::IMailboxServant* servant_for(std::uint16_t gen) noexcept;
    void refill_one() noexcept;
    [[nodiscard]] bool rest() const noexcept;

    void release() noexcept;

    void on(const CompanionBind::Attach& a) noexcept;
    void on(const CompanionBind::Bind& b) noexcept;
    void misrouted(const CompanionBind& m) noexcept;

    [[nodiscard]] std::uint32_t attaches() const noexcept { return attaches_.get(); }
    [[nodiscard]] std::uint32_t binds_applied() const noexcept { return applied_.get(); }
    [[nodiscard]] std::uint32_t binds_unheld() const noexcept { return unheld_.get(); }

private:
    Binds binds_;
    cores::ServantSet servants_{};
    cores::IMailboxServant* held_ = nullptr;
    std::uint16_t bound_gen_ = 0;
    xthread::Counter attaches_, applied_, unheld_;
};

}  // namespace mister::app
