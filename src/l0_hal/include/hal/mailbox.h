// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "infra/claim_set.h"
#include "infra/seat.h"
#include "hal/block_view.h"
#include "hal/gpi_block.h"
#include "hal/gpo_block.h"
#include "hal/link_timing.h"

namespace mister::hal {

enum class MailboxId : std::uint8_t { SpiMaster };
inline constexpr std::size_t kMailboxCount = 1;
using MailboxClaims = infra::ClaimSet<MailboxId, kMailboxCount>;

struct MailboxRegs {
    BlockView<GpoBlock> gpo;
    BlockView<GpiBlock> gpi;
};

class Mailbox {
    TASTY_SEAT_EXEMPT(component);

public:
    Mailbox(Mailbox&& o) noexcept
        : regs_(std::exchange(o.regs_, MailboxRegs{})),
          timing_(o.timing_),
          claim_(std::move(o.claim_)) {}

    Mailbox& operator=(Mailbox&&) = delete;
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    ~Mailbox() = default;

    [[nodiscard]] BlockView<GpoBlock> gpo() const noexcept { return regs_.gpo; }

    [[nodiscard]] BlockView<GpiBlock> gpi() const noexcept { return regs_.gpi; }

    [[nodiscard]] const LinkTimingValues& timing() const noexcept { return timing_; }

    [[nodiscard]] bool held() const noexcept { return static_cast<bool>(claim_); }

    [[nodiscard]] MailboxClaims::Claim consume() && noexcept {
        regs_ = MailboxRegs{};
        return std::move(claim_);
    }

private:
    friend class BoardWindows;

    Mailbox(MailboxRegs regs, const LinkTimingValues& timing, MailboxClaims::Claim claim) noexcept
        : regs_(regs),
          timing_(timing),
          claim_(std::move(claim)) {}

    MailboxRegs regs_;
    LinkTimingValues timing_;
    MailboxClaims::Claim claim_;
};

}
