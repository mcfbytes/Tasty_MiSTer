// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/lw_host_enable.h"
#include "hal/enable_word.h"
#include "hal/entry_mask.h"
#include "hal/reset_counts.h"

#include <utility>

namespace mister::app {

namespace {
using Reg = hal::SysDirectory::Reg;
}

LwHostEnable::LwHostEnable(
    BayCounts& counts,
    BayCountsCell& cell,
    const os::IClock& clock,
    infra::OptRef<os::IDelay> delay
) noexcept
    : counts_(counts),
      cell_(cell),
      clock_(clock),
      delay_(delay) {}

void LwHostEnable::attach(hal::reg::Lend header) noexcept {
    unmapped_ = false;
    window_.emplace(hal::reg::Block::redeem(header));
    port_ = infra::OptRef<hal::IRegPort<hal::SysDirectory>>{*window_};
}

void LwHostEnable::attach(hal::IRegPort<hal::SysDirectory>& port) noexcept {
    unmapped_ = false;
    window_.reset();
    port_ = infra::OptRef<hal::IRegPort<hal::SysDirectory>>{port};
}

void LwHostEnable::attach_unmapped() noexcept {
    window_.reset();
    port_.reset();
    unmapped_ = true;
}

void LwHostEnable::drop() noexcept {
    window_.reset();
    port_.reset();
    unmapped_ = false;
    clear_masks();
}

void LwHostEnable::clear_masks() noexcept {
    linked_ = {};
    lines_ = {};
    any_ = false;
}

void LwHostEnable::note_grant(
    proto::DirEntryKind kind,
    hal::SysDirectory::EntryIndex at,
    bool carries_line
) noexcept {
    any_ = true;
    const hal::EntryMask bit = hal::entry_bit(at);
    if (proto::linked(kind))
        linked_ = linked_.with(bit);
    if (carries_line)
        lines_ = lines_.with(bit);
}

Ex<void> LwHostEnable::take() noexcept {
    if (!served())
        return {};
    if (!port_)
        return refuse_(Errc::io, hal::RegWord{});

    const hal::EnableWord first = enable_word_();
    if (first.enabled() || first.id_view() || first.hard_reset()) {
        return refuse_(Errc::bridge_state, first.register_word());
    }
    if (!poll_quiet_())
        return refuse_(Errc::timeout, counts_.owned_last.register_word());
    port_->write(Reg::Enable, hal::EnableWord::key());

    const hal::EnableWord after = enable_word_();
    if (!after.enabled())
        return refuse_(Errc::bridge_state, after.register_word());
    counts_.enables.add();
    if (!poll_open_()) {
        counts_.open_refusals.add();
        return refuse_(Errc::timeout, port_->read(Reg::Open));
    }
    counts_.resets_last = hal::ResetCounts::of(port_->read(Reg::Resets));
    publish_();
    return {};
}

void LwHostEnable::arm_lines() noexcept {
    if (!served() || !port_)
        return;
    port_->write(Reg::LineEn, lines_.register_word());
}

Ex<void> LwHostEnable::confirm() noexcept {
    if (!served() || !port_)
        return {};
    const hal::EnableWord now = enable_word_();
    if (now.enabled())
        return {};
    counts_.enable_lost.add();
    publish_();
    return std::unexpected(now.register_word().refusal(Errc::bridge_state, ERR_SITE()));
}

hal::EnableWord LwHostEnable::enable_word_() noexcept {
    return hal::EnableWord::of(port_->read(Reg::Enable));
}

bool LwHostEnable::poll_quiet_() noexcept {
    const auto start = clock_.now();
    for (;;) {
        if (enable_word_().quiet())
            return true;
        if (since_(start) >= kQuietBound) {
            counts_.owned_last = hal::EntryMask::of(port_->read(Reg::Owned));
            return false;
        }
        delay_ref_().sleep_for(kPollGap);
    }
}

bool LwHostEnable::poll_open_() noexcept {
    const auto start = clock_.now();
    for (;;) {
        const hal::EntryMask open = hal::EntryMask::of(port_->read(Reg::Open));
        if (open.covers(linked_))
            return true;
        if (since_(start) >= kOpenBound)
            return false;
        delay_ref_().sleep_for(kPollGap);
    }
}

std::chrono::microseconds LwHostEnable::since_(
    os::RawMonotonicClock::time_point start
) const noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(clock_.now() - start);
}

Ex<void> LwHostEnable::refuse_(Errc why, hal::RegWord detail) noexcept {
    counts_.enable_refusals.add();
    publish_();
    return std::unexpected(detail.refusal(why, ERR_SITE()));
}

void LwHostEnable::publish_() noexcept {
    cell_.publish(counts_);
}

}

namespace mister::hal {
template class WindowPort<SysDirectory, seat_owner::Main>;
}
