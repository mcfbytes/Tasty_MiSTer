// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "infra/claim_set.h"
#include "infra/error.h"
#include "hal/axi.h"
#include "hal/board_profile.h"
#include "hal/mailbox.h"
#include "hal/register_window.h"
#include "hal/window_decl.h"

namespace mister::hal {

template <class Layout>
struct TakenWindow {
    RegisterWindow<Layout> regs;
    WindowClaim claim;
};

struct WindowImage {
    WindowId id;
    volatile void* base;
    std::size_t len;
};

enum class FallbackNotice : std::uint8_t { Announce, Quiet };

class BoardWindows {
public:
    [[nodiscard]] static Ex<BoardWindows> open(const BoardProfile& profile,
                                               FallbackNotice notice = FallbackNotice::Announce);

    [[nodiscard]] static Ex<BoardWindows> attach(const BoardProfile& profile,
                                                 std::span<const WindowImage> images);

    BoardWindows(BoardWindows&& o) noexcept
        : profile_(o.profile_), pages_(std::move(o.pages_)), lends_(std::exchange(o.lends_, {})),
          window_claims_(std::move(o.window_claims_)),
          mailbox_claims_(std::move(o.mailbox_claims_)) {}
    BoardWindows& operator=(BoardWindows&&) = delete;
    BoardWindows(const BoardWindows&) = delete;
    BoardWindows& operator=(const BoardWindows&) = delete;

    const BoardProfile& profile() const noexcept { return *profile_; }

    template <class Layout>
    [[nodiscard]] Ex<TakenWindow<Layout>> take(WindowId id) {
        auto lead = lead_of(id, Layout::kSize);
        if (!lead) return std::unexpected(lead.error());
        auto claim = window_claims_->claim(id);
        if (!claim) return std::unexpected(claim.error());
        return TakenWindow<Layout>{RegisterWindow<Layout>::borrow(*lead, Layout::kSize),
                                   std::move(*claim)};
    }

    [[nodiscard]] Ex<Mailbox> take_mailbox();

    [[nodiscard]] infra::ClaimSetCounts window_counts() const noexcept {
        return window_claims_->counts();
    }
    [[nodiscard]] infra::ClaimSetCounts mailbox_counts() const noexcept {
        return mailbox_claims_->counts();
    }

private:
    struct Lend {
        volatile void* base = nullptr;
        std::size_t len = 0;
    };

    explicit BoardWindows(const BoardProfile& profile) : profile_(&profile) {}

    [[nodiscard]] Ex<volatile void*> lead_of(WindowId id, std::size_t need) const;
    [[nodiscard]] Ex<volatile std::uint32_t*> mailbox_gpo() const;
    [[nodiscard]] Ex<const volatile std::uint32_t*> mailbox_gpi() const;

    const BoardProfile* profile_;
    std::array<std::optional<detail::MappedRegs>, kMaxWindows> pages_{};
    std::array<Lend, kMaxWindows> lends_{};

    std::unique_ptr<WindowClaims> window_claims_ = std::make_unique<WindowClaims>();
    std::unique_ptr<MailboxClaims> mailbox_claims_ = std::make_unique<MailboxClaims>();
};

}  // namespace mister::hal
