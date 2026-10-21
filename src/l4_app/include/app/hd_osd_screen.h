// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/hd_flip_slot.h"
#include "app/hd_osd_ask.h"
#include "app/hd_osd_status.h"
#include "app/hd_surface.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "os/clock.h"
#include "os/vt_console.h"

namespace mister::app {

class HpsFramebuffer;
class VideoPump;

class HdOsdScreen {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Wiring {
        HdOsdAskCell& ask;
        const HdOsdStatusCell& status;
        HdFlipChannel& flips;
        xthread::WakeFlag& wake;
        HpsFramebuffer& fb;
        const VideoPump& video;
        const os::IClock& clock;
        infra::OptRef<os::IVtConsole> vt{};
    };

    explicit HdOsdScreen(const Wiring& w) noexcept : w_(w) {}

    HdOsdScreen(const HdOsdScreen&) = delete;
    HdOsdScreen& operator=(const HdOsdScreen&) = delete;

    void tick(const PageDescription* page, HdOsdStatus::Badge badge) noexcept;
    [[nodiscard]] bool intends_hdmi() const noexcept;
    [[nodiscard]] bool covers_hdmi() const noexcept;
    [[nodiscard]] bool available() const noexcept;

    void release_console() noexcept;
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
    void sync_console_() noexcept;
    void capture_console_() noexcept;
    void release_console_() noexcept;

    Wiring w_;
    HdOsdStatusCell::Reader status_seen_{};
    HdOsdStatus status_{};
    std::optional<HdFlipChannel::Job> shown_{};
    std::optional<HdFlipChannel::Job> owed_{};
    PageDescription page_{};
    PageDescription page_sent_{};
    HdOsdStatus::Badge badge_ = HdOsdStatus::Badge::Unknown;
    HdOsdStatus::Badge badge_sent_ = HdOsdStatus::Badge::Unknown;
    HdSurface surface_{};
    std::uint32_t epoch_ = 0;
    std::uint32_t applies_seen_ = 0;
    std::uint32_t out_w_ = 0;
    std::uint32_t out_h_ = 0;
    std::uint32_t stale_drops_ = 0;
    int guarded_vt_ = 0;
    bool open_ = false;
    bool intends_ = false;
    bool covers_ = false;
    bool ours_ = false;
    bool reraise_owed_ = false;
    bool drop_owed_ = false;
};

}
