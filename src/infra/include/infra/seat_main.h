// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>

#include "infra/park_like.h"

namespace mister::xthread {

template <class B>
concept SeatBody = requires(B& b, const B& cb) {
    { b.serve() } noexcept -> std::same_as<void>;
    { cb.idle() } noexcept -> std::same_as<bool>;
    { cb.stopping() } noexcept -> std::same_as<bool>;
    { cb.park_ms() } noexcept -> std::same_as<int>;
    { b.park() } noexcept -> ParkLike;
    { b.settle() } noexcept -> std::same_as<void>;
    { b.paused() } noexcept -> std::same_as<bool>;
    { cb.pause_pending() } noexcept -> std::same_as<bool>;
};

template <class Derived>
class SeatMain {
public:
    void run_once() noexcept {
        if (!self_().paused()) self_().serve();
    }

protected:
    void loop_() noexcept {
        Derived& d = self_();
        bool stopping = false;
        bool paused = false;
        for (;;) {
            if (!paused) d.serve();
            paused = d.paused();
            if (!stopping) stopping = d.stopping();
            if (stopping && (paused || d.idle())) break;
            if (paused || d.idle()) {
                auto& p = d.park();
                p.arm();
                if (!d.pause_pending() && (paused || d.idle())) p.wait(d.park_ms());
                p.disarm();
            }
        }
        d.settle();
    }

private:
    Derived& self_() noexcept { return static_cast<Derived&>(*this); }
};

}  // namespace mister::xthread
