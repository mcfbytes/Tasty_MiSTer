// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "infra/xthread/claim_set.h"
#include "infra/error.h"
#include "hal/axi.h"
#include "hal/board_profile.h"
#include "hal/gpo_gpi_pair.h"
#include "hal/reg_block.h"
#include "hal/reg_lend.h"
#include "hal/register_window.h"
#include "hal/taken_window.h"
#include "hal/window_decl.h"
#include "infra/prelude.h"

namespace mister::hal {

struct WindowImage {
    WindowId id;
    std::span<std::uint32_t> words;
};

enum class FallbackNotice : std::uint8_t { Announce, Quiet };

class BoardWindows {
public:
    [[nodiscard]] static Ex<BoardWindows> open(
        const BoardProfile& profile,
        FallbackNotice notice = FallbackNotice::Announce
    );

    BoardWindows(BoardWindows&& o) noexcept
        : profile_(o.profile_),
          pages_(std::move(o.pages_)),
          lends_(std::exchange(o.lends_, {})),
          window_claims_(std::move(o.window_claims_)),
          pair_claims_(std::move(o.pair_claims_)) {}

    BoardWindows& operator=(BoardWindows&&) = delete;
    BoardWindows(const BoardWindows&) = delete;
    BoardWindows& operator=(const BoardWindows&) = delete;

    const BoardProfile& profile() const noexcept { return *profile_; }

    template <class Layout>
    [[nodiscard]] Ex<TakenWindow<Layout>> take(WindowId id) {
        auto lead = lead_of(id, Layout::kSize);
        if (!lead)
            return std::unexpected(lead.error());
        auto claim = window_claims_->claim(id);
        if (!claim)
            return std::unexpected(claim.error());
        return TakenWindow<Layout>{
            RegisterWindow<Layout>::borrow(std::move(*lead)),
            std::move(*claim)
        };
    }

    [[nodiscard]] Ex<GpoGpiPair> take_gpo_gpi();

    [[nodiscard]] xthread::ClaimSetCounts window_counts() const noexcept {
        return window_claims_->counts();
    }

    [[nodiscard]] xthread::ClaimSetCounts pair_counts() const noexcept {
        return pair_claims_->counts();
    }

private:
    friend struct BoardWindowsTestAccess;

    explicit BoardWindows(const BoardProfile& profile) : profile_(&profile) {}

    [[nodiscard]] static Ex<BoardWindows> attach(
        const BoardProfile& profile,
        std::span<const WindowImage> images
    );

    [[nodiscard]] Ex<reg::Block> lead_of(WindowId id, std::size_t need) const;

    template <class Layout>
    [[nodiscard]] Ex<BlockView<Layout>> pair_reg(FactField field) const;

    [[nodiscard]] Ex<void> bind_slot(WindowId id, reg::Lend lend);
    [[nodiscard]] Ex<BlockView<GpoBlock>> pair_gpo() const;
    [[nodiscard]] Ex<BlockView<GpiBlock>> pair_gpi() const;

    const BoardProfile* profile_;
    std::array<std::optional<detail::MappedRegs>, kMaxWindows> pages_{};
    std::array<reg::Lend, kMaxWindows> lends_{};

    std::unique_ptr<WindowClaims> window_claims_ = std::make_unique<WindowClaims>();
    std::unique_ptr<GpoGpiClaims> pair_claims_ = std::make_unique<GpoGpiClaims>();
};

}
