// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/window_job_service.h"

#include <span>

namespace mister::app {

WindowJobService::Chan::Loan WindowJobService::acquire_() noexcept { return chan_.acquire(); }

void WindowJobService::send_(Chan::Loan&& loan, const cores::IWindowJobKind* kind) noexcept {
    loan->kind = kind;
    loan->ok = 0;
    armed_ = true;
    chan_.send(std::move(loan));
}

bool WindowJobService::arm_osd_open(cores::IWindowSave& role) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::arm_osd_open off T-RT");
    auto loan = acquire_();
    if (!loan) {
        refusals_.add(1);
        return false;
    }
    const cores::IWindowJobKind* job = role.on_osd_open(std::span<std::byte>(loan->order));
    if (job == nullptr) return false;
    send_(std::move(loan), job);
    return true;
}

bool WindowJobService::arm_reset_edge(cores::IWindowSave& role,
                                      const proto::ResetEdge& edge) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::arm_reset_edge off T-RT");
    auto loan = acquire_();
    if (!loan) {
        refusals_.add(1);
        return false;
    }
    const cores::IWindowJobKind* job = role.on_reset_edge(edge, std::span<std::byte>(loan->order));
    if (job == nullptr) return false;
    send_(std::move(loan), job);
    return true;
}

bool WindowJobService::hold_open(const cores::IWindowSave& role, proto::IoIndex index,
                                 std::uint32_t act) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::hold_open off T-RT");
    if (held_ != Held::None || !role.owes_flush(index)) return false;
    held_ = Held::Waiting;
    held_act_ = act;
    held_index_ = index;
    return true;
}

std::optional<std::uint32_t> WindowJobService::service(cores::IWindowSave* role) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::service off T-RT");
    if (auto done = chan_.reap()) {
        armed_ = false;
        if (done.completed() && done->ok != 0) {
            ok_.add(1);
        } else {
            failed_.add(1);
        }
        if (held_ == Held::Flushing) {
            held_ = Held::None;
            return held_act_;
        }
    }
    if (held_ != Held::Waiting || armed_) return std::nullopt;

    auto loan = acquire_();
    const cores::IWindowJobKind* job =
        (loan && role != nullptr) ? role->take_flush(held_index_, std::span<std::byte>(loan->order))
                                  : nullptr;
    if (job == nullptr) {
        held_ = Held::None;
        return held_act_;
    }
    flushes_.add(1);
    held_ = Held::Flushing;
    send_(std::move(loan), job);
    return std::nullopt;
}

void WindowJobService::forget_held() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::forget_held off T-RT");
    held_ = Held::None;
    held_act_ = 0;
    held_index_ = proto::IoIndex{};
}

void WindowJobService::finish_(bool ok, Error err) noexcept {
    WindowJobSlot& slot = *job_;
    if (running_ != nullptr) {
        running_->~IWindowJob();
        running_ = nullptr;
    }
    slot.kind = nullptr;
    window_.reset();
    slot.ok = ok ? 1u : 0u;
    slot.err = err;
    job_.complete();
    job_ = Chan::Job{};
}

void WindowJobService::serve() noexcept {
    if (!job_) {
        job_ = chan_.take();
        if (!job_) return;
        WindowJobSlot& slot = *job_;
        running_ = slot.kind != nullptr ? slot.kind->build(std::span<std::byte>(slot.arena),
                                                           std::span<const std::byte>(slot.order))
                                        : nullptr;
        if (running_ == nullptr) {
            finish_(false, Error{Errc::bad_format, ERR_SITE(), 0});
            return;
        }
    }
    cores::IWindowJob& job = *running_;
    if (!window_) {
        auto w =
            LoadWindow::open(*map_, aperture_, os::PhysAddr{job.window_addr()}, job.window_len());
        if (!w) {
            finish_(false, w.error());
            return;
        }
        window_.emplace(std::move(*w));
    }
    auto more = job.step(*window_, *vfs_);
    if (!more) {
        finish_(false, more.error());
        return;
    }
    if (!*more) finish_(true, Error{Errc::io, 0, 0});
}

WindowJobService::~WindowJobService() {
    if (running_ != nullptr) running_->~IWindowJob();
}

bool WindowJobService::idle() const noexcept { return !job_ && chan_.outbound() == 0; }

}  // namespace mister::app
