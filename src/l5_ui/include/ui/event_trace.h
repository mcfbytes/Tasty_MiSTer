// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/event.h"
#include "infra/diag_log.h"
#include "infra/seat.h"

namespace mister::ui {

class EventTrace {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::uint32_t kMaxPerSec = 64;

    explicit EventTrace(app::EventQueue<SeatTag::RT>& queue) noexcept : queue_(&queue) {}

    bool open(const char* path) noexcept { return log_.open(path); }

    void set_echo_stderr(bool on) noexcept { log_.set_echo_stderr(on); }

    void record(const app::Event::CoreLoaded& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::SessionFailed& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::SdActivity& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::InfoRequest& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::ProgressUpdate& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::DeadlineMiss& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::RequestRefused& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::ConfStrOnlySession& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::SessionAdvisory& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::SessionEnded& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::RamImageDeclined& a, const app::Event::Head& h) noexcept;
    void record(const app::Event::RequestDone& a, const app::Event::Head& h) noexcept;

    void record_misrouted() noexcept;

    std::uint32_t records() const noexcept { return seq_; }

    std::uint32_t suppressed() const noexcept { return suppressed_; }

private:
    [[nodiscard]] bool admit_() noexcept;

    void write_(
        const char* name,
        const char* fields,
        app::Event::Kind k,
        const app::Event::Head& h
    ) noexcept;

    app::EventQueue<SeatTag::RT>* queue_;
    xthread::DiagLog log_;
    std::uint32_t seq_ = 0;
    std::uint32_t suppressed_ = 0;
    std::uint32_t window_count_ = 0;
    std::int64_t window_start_ns_ = 0;
};

}
