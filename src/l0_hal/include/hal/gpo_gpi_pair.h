// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include "infra/xthread/claim_set.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "hal/block_view.h"
#include "hal/gpi_block.h"
#include "hal/gpo_block.h"
#include "hal/link_timing.h"

namespace mister::hal {

enum class GpoGpiPairId : std::uint8_t { Link };
inline constexpr std::size_t kGpoGpiPairCount = 1;
using GpoGpiClaims = xthread::ClaimSet<GpoGpiPairId, kGpoGpiPairCount>;

struct GpoGpiRegs {
    BlockView<GpoBlock> gpo;
    BlockView<GpiBlock> gpi;
};

class GpoGpiPair {
    TASTY_SEAT_EXEMPT(component);

public:
    GpoGpiPair(GpoGpiPair&& o) noexcept
        : regs_(std::exchange(o.regs_, GpoGpiRegs{})),
          timing_(o.timing_),
          claim_(std::move(o.claim_)) {}

    GpoGpiPair& operator=(GpoGpiPair&&) = delete;
    GpoGpiPair(const GpoGpiPair&) = delete;
    GpoGpiPair& operator=(const GpoGpiPair&) = delete;
    ~GpoGpiPair() = default;

    [[nodiscard]] BlockView<GpoBlock> gpo() const noexcept { return regs_.gpo; }

    [[nodiscard]] BlockView<GpiBlock> gpi() const noexcept { return regs_.gpi; }

    [[nodiscard]] const LinkTimingValues& timing() const noexcept { return timing_; }

    [[nodiscard]] bool held() const noexcept { return static_cast<bool>(claim_); }

    [[nodiscard]] GpoGpiClaims::Claim consume() && noexcept {
        regs_ = GpoGpiRegs{};
        return std::move(claim_);
    }

private:
    friend class BoardWindows;

    GpoGpiPair(GpoGpiRegs regs, const LinkTimingValues& timing, GpoGpiClaims::Claim claim) noexcept
        : regs_(regs),
          timing_(timing),
          claim_(std::move(claim)) {}

    GpoGpiRegs regs_;
    LinkTimingValues timing_;
    GpoGpiClaims::Claim claim_;
};

}
