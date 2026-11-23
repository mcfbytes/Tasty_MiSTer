// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cmos_keeper.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include "cores/cmos_image.h"
#include "infra/byte_view.h"
#include "os/local_time.h"
#include "proto/link_op.h"
#include "proto/user_io/poll_shape.h"

namespace mister::app {

namespace {

constexpr hal::SpiWord kCountMask = proto::user_io::poll_row(
                                        proto::user_io::PollShape::ChangeCounter
)
                                        .mask.wire_word();

}

void CmosKeeper::bind(infra::OptRef<const cores::CmosLayout> layout) noexcept {
    TASTY_SEAT_BODY(CmosKeeper);
    layout_ = layout;
    count_ = pulled_ = hal::SpiWord{};
    clock_run_.reset();
    save_due_ = os::Deadline::immediate();
    clock_due_ = os::Deadline::immediate();
}

void CmosKeeper::on_change_count(hal::SpiWord word) noexcept {
    TASTY_SEAT_BODY(CmosKeeper);
    count_ = word.masked(kCountMask);
}

void CmosKeeper::tick(cores::ILadderHost& host) {
    TASTY_SEAT_BODY(CmosKeeper);
    if (!layout_)
        return;
    const os::RawMonotonicClock::time_point now = clock_.now();
    if (save_due_.expired_at(now)) {
        save_due_ = os::Deadline::in(clock_, kSavePeriod);

        if (count_ != pulled_ && host.order(proto::LinkOp::SaveUpload{})) {
            pulled_ = count_;
            pulls_ordered_.add();
        }
    }
    if (clock_run_ || clock_due_.expired_at(now))
        step_clock_(host);
}

void CmosKeeper::step_clock_(cores::ILadderHost& host) {
    if (!clock_run_) {
        clock_due_ = os::Deadline::in(clock_, kClockPeriod);
        const auto now = os::local_now();
        if (!now)
            return;
        const cores::CmosClock composed = cores::compose_rtc(*now);
        const auto bytes = infra::byte_cast<const std::uint8_t>(std::span(composed.registers));
        auto run = cores::PayloadPieces::held_window(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
            cores::PayloadPieces::HeldFrame{
                .dest = layout_->index,
                .framing = host.link_width(),
                .piece = kClockPieceWords
            }
        );
        if (!run)
            return;
        clock_run_ = std::move(*run);
    }
    const auto pass = clock_run_->step(host);
    if (pass && *pass != cores::PayloadPieces::Pass::Done)
        return;
    if (pass)
        clocks_sent_.add();
    clock_run_.reset();
}

std::optional<os::Deadline> CmosKeeper::next_due() const noexcept {
    if (!layout_)
        return std::nullopt;
    if (clock_run_)
        return os::Deadline::immediate();
    return save_due_.at() < clock_due_.at() ? save_due_ : clock_due_;
}

}
