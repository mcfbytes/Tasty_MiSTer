// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/window_job_service.h"

#include <span>

namespace mister::app {

WindowJobService::Chan::Loan WindowJobService::acquire_() noexcept {
    return chan_.acquire();
}

void WindowJobService::send_(Chan::Loan&& loan, const cores::IWindowJobKind* kind) noexcept {
    loan->kind = infra::opt_ref_of(kind);
    loan->ok = 0;
    rt_.armed = true;
    chan_.send(std::move(loan));
}

bool WindowJobService::arm_osd_open(cores::IWindowSave& role) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::arm_osd_open off T-RT");
    auto loan = acquire_();
    if (!loan) {
        rt_.refusals.add(1);
        return false;
    }
    const cores::IWindowJobKind* job = role.on_osd_open(std::span<std::byte>(loan->order));
    if (job == nullptr)
        return false;
    send_(std::move(loan), job);
    return true;
}

bool WindowJobService::arm_reset_edge(
    cores::IWindowSave& role,
    const proto::ResetEdge& edge
) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::arm_reset_edge off T-RT");
    auto loan = acquire_();
    if (!loan) {
        rt_.refusals.add(1);
        return false;
    }
    const cores::IWindowJobKind* job = role.on_reset_edge(edge, std::span<std::byte>(loan->order));
    if (job == nullptr)
        return false;
    send_(std::move(loan), job);
    return true;
}

bool WindowJobService::hold_open(
    const cores::IWindowSave& role,
    proto::IoIndex index,
    std::uint32_t act
) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::hold_open off T-RT");
    if (rt_.held != Held::None || !role.owes_flush(index))
        return false;
    rt_.held = Held::Waiting;
    rt_.held_act = act;
    rt_.held_index = index;
    return true;
}

std::optional<std::uint32_t> WindowJobService::service(cores::IWindowSave* role) noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::service off T-RT");
    if (auto done = chan_.reap()) {
        rt_.armed = false;
        if (done.completed() && done->ok != 0) {
            rt_.ok.add(1);
        } else {
            rt_.failed.add(1);
        }
        if (rt_.held == Held::Flushing) {
            rt_.held = Held::None;
            return rt_.held_act;
        }
    }
    if (rt_.held != Held::Waiting || rt_.armed)
        return std::nullopt;

    auto loan = acquire_();
    const cores::IWindowJobKind* job = (loan && role != nullptr)
        ? role->take_flush(rt_.held_index, std::span<std::byte>(loan->order))
        : nullptr;
    if (job == nullptr) {
        rt_.held = Held::None;
        return rt_.held_act;
    }
    rt_.flushes.add(1);
    rt_.held = Held::Flushing;
    send_(std::move(loan), job);
    return std::nullopt;
}

void WindowJobService::forget_held() noexcept {
    seat_assert<SeatTag::RT>(ERR_SITE(), "WindowJobService::forget_held off T-RT");
    rt_.held = Held::None;
    rt_.held_act = 0;
    rt_.held_index = proto::IoIndex{};
}

void WindowJobService::finish_(bool ok, Error err) noexcept {
    WindowJobSlot& slot = *io_.job;
    io_.running.reset();
    slot.kind.reset();
    io_.window.reset();
    slot.ok = ok ? 1u : 0u;
    slot.err = err;
    io_.job.complete();
    io_.job = Chan::Job{};
}

void WindowJobService::serve() noexcept {
    if (!io_.job) {
        io_.job = chan_.take();
        if (!io_.job)
            return;
        WindowJobSlot& slot = *io_.job;
        io_.running.reset(
            slot.kind ? slot.kind->build(
                            std::span<std::byte>(slot.arena),
                            std::span<const std::byte>(slot.order)
                        )
                      : nullptr
        );
        if (!io_.running) {
            finish_(false, Error{Errc::bad_format, ERR_SITE(), 0});
            return;
        }
    }
    cores::IWindowJob& job = *io_.running;
    if (!io_.window) {
        auto w = Window::open(*map_, aperture_, os::PhysAddr{job.window_addr()}, job.window_len());
        if (!w) {
            finish_(false, w.error());
            return;
        }
        io_.window.emplace(std::move(*w));
    }
    auto more = job.step(*io_.window, *vfs_);
    if (!more) {
        finish_(false, more.error());
        return;
    }
    if (!*more)
        finish_(true, Error{Errc::io, 0, 0});
}

bool WindowJobService::idle() const noexcept {
    return !io_.job && chan_.outbound() == 0;
}

}
