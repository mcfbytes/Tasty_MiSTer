// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/hd_flip_slot.h"
#include "app/hd_osd_ask.h"
#include "app/hd_osd_status.h"
#include "app/hd_surface.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

class HpsFramebuffer;
class VideoPump;

class HdOsdScreen {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        HdOsdAskCell* ask = nullptr;
        const HdOsdStatusCell* status = nullptr;
        HdFlipChannel* flips = nullptr;
        xthread::WakeFlag* wake = nullptr;
        HpsFramebuffer* fb = nullptr;
        const VideoPump* video = nullptr;
    };

    explicit HdOsdScreen(const Wiring& w) noexcept : w_(w) {}
    HdOsdScreen(const HdOsdScreen&) = delete;
    HdOsdScreen& operator=(const HdOsdScreen&) = delete;

    void tick(const PageDescription* page) noexcept;
    [[nodiscard]] bool covers_hdmi() const noexcept;
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] std::uint32_t stale_drops() const noexcept;

private:
    void close_() noexcept;
    void retry_drop_() noexcept;
    void publish_ask_(bool is_open) noexcept;
    [[nodiscard]] bool try_open_() noexcept;
    void on_applies_() noexcept;
    void take_jobs_() noexcept;
    void raise_job_(HdFlipChannel::Job job) noexcept;
    [[nodiscard]] bool reraise_() noexcept;

    Wiring w_;
    HdOsdStatusCell::Reader status_seen_{};
    HdOsdStatus status_{};
    std::optional<HdFlipChannel::Job> shown_{};
    std::optional<HdFlipChannel::Job> owed_{};
    PageDescription page_{};
    PageDescription page_sent_{};
    HdSurface surface_{};
    std::uint32_t epoch_ = 0;
    std::uint32_t applies_seen_ = 0;
    std::uint32_t out_w_ = 0;
    std::uint32_t out_h_ = 0;
    std::uint32_t stale_drops_ = 0;
    bool open_ = false;
    bool covers_ = false;
    bool ours_ = false;
    bool reraise_owed_ = false;
    bool drop_owed_ = false;
};

}  // namespace mister::app
