// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "app/ocram_frame_grant.h"
#include "hal/ocram.h"
#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/link/frame_lane.h"
#include "proto/link_sel.h"

namespace mister::app {

class OcramFrameLane final : public proto::link::IFrameLane {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kStageWords = 64;
    static constexpr std::size_t kMaxPlays = 2;

    struct Counts {
        infra::Tally records{};
        infra::Tally posted{};
        infra::Tally refused{};
        infra::Tally abandoned{};
    };

    OcramFrameLane() = default;
    OcramFrameLane(const OcramFrameLane&) = delete;
    OcramFrameLane& operator=(const OcramFrameLane&) = delete;

    [[nodiscard]] bool bind(const OcramFrameGrant& grant) noexcept;

    void unbind() noexcept;

    [[nodiscard]] bool bound() const noexcept { return lend_.lent(); }

    [[nodiscard]] bool holds_writer() const noexcept { return ring_.has_value(); }

    [[nodiscard]] bool suspended() const noexcept { return suspended_; }

    [[nodiscard]] bool posted() const noexcept { return posted_; }

    [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

    using proto::ILinkFrame::begin;
    [[nodiscard]] Ex<void> begin(proto::LinkSel sel, hal::LinkWord op) override;
    [[nodiscard]] Ex<void> word(hal::LinkWord w) override;

    [[nodiscard]] Ex<void> end() override;
    [[nodiscard]] std::optional<hal::LinkWord> opcode_reply() const noexcept override;

    void settle() noexcept override;
    [[nodiscard]] bool busy() const noexcept override;
    void abandon() noexcept override;
    void suspend() noexcept override;
    void resume() noexcept override;

private:
    struct Plays {
        std::array<hal::ocram::LaneArg::Sel, kMaxPlays> sel{};
        std::size_t count = 0;

        void add_served(const hal::ocram::LaneShape& shape, hal::ocram::LaneArg::Sel s) noexcept {
            if (count < sel.size() && shape.serves(s))
                sel[count++] = s;
        }
    };

    [[nodiscard]] Plays plays_(proto::LinkSel sel) const noexcept;

    [[nodiscard]] bool publish_() noexcept;
    void redeem_() noexcept;
    void drop_posted_() noexcept;

    hal::ocram::RingLend lend_{};
    std::optional<hal::ocram::LaneShape> shape_{};
    std::optional<hal::ocram::RingWriter<seat_owner::RT>> ring_{};
    std::array<hal::LinkWord, kStageWords> stage_{};
    std::size_t staged_ = 0;
    proto::LinkSel sel_ = proto::LinkSel::Io;
    bool open_ = false;
    bool posted_ = false;
    bool unplayed_ = false;
    bool suspended_ = false;
    Counts counts_{};
};

}
