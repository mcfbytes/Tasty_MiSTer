// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "app/audio_pump.h"
#include "app/ini_cell.h"
#include "app/input_gates_cell.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/prelude_composer.h"
#include "app/prelude_stats.h"
#include "app/video_pump.h"
#include "app/video_wire.h"
#include "infra/message_sum.h"
#include "os/raw_monotonic_clock.h"
#include "proto/link_event.h"
#include "svc/vfs.h"
#include "proto/link_router.h"
#include "support/fake_link_router.h"
#include "support/stand_ini.h"
#include "support/temp_root.h"

namespace mister::testing {

struct PreludeClock final : os::IClock {
    std::int64_t ns = 0;

    [[nodiscard]] os::RawMonotonicClock::time_point now() const override {
        return os::RawMonotonicClock::time_point{std::chrono::nanoseconds{ns}};
    }
};

[[nodiscard]] inline app::VideoWire::Wiring video_wiring(
    app::LinkTxChannel<SeatTag::Ui>& link_tx,
    proto::ILinkRouter& replies
) {
    return app::VideoWire::Wiring{.link_tx = link_tx, .replies = replies};
}

[[nodiscard]] inline app::VideoPump::Wiring video_wiring(
    const xthread::Counter& activity,
    const app::InputGatesCell& gates,
    app::LinkTxChannel<SeatTag::Ui>& link_tx,
    proto::ILinkRouter& replies,
    const app::IniCell& ini = kStandIniCell
) {
    return app::VideoPump::Wiring{
        .activity = activity,
        .gates = gates,
        .link_tx = link_tx,
        .replies = replies,
        .ini = ini
    };
}

inline bool drain_replies(app::VideoPump& pump, app::LinkRxChannel<SeatTag::Ui>& rx) {
    bool any = false;
    while (const auto ev = rx.pop()) {
        if (const auto r = infra::as<proto::LinkEvent::FrameReply>(*ev)) {
            pump.on_frame_reply(*r);
            any = true;
        }
    }
    return any;
}

inline const svc::Vfs& stand_vfs() {
    static TempRoot root{"tasty_prelude_stand"};
    static const Ex<svc::Vfs> vfs = svc::Vfs::create_at(root.path());
    if (!vfs)
        std::abort();
    return *vfs;
}

class PreludeStand {
public:
    PreludeStand(
        app::VideoWire& wire,
        app::LinkRxChannel<SeatTag::Ui>& ui_rx,
        const svc::Vfs& vfs = stand_vfs()
    ) noexcept
        : wire_(wire),
          ui_rx_(ui_rx),
          vfs_(vfs),
          clock_{},
          composer_({.wire = wire, .audio = audio_, .clock = clock_}) {}

    PreludeStand(const PreludeStand&) = delete;
    PreludeStand& operator=(const PreludeStand&) = delete;

    [[nodiscard]] app::PreludeComposer& composer() noexcept { return composer_; }

    [[nodiscard]] app::AudioPump& audio() noexcept { return audio_; }

    [[nodiscard]] const svc::Vfs& vfs() const noexcept { return vfs_; }

    [[nodiscard]] PreludeClock& clock() noexcept { return clock_; }

    void core_loaded(bool front_end = false) noexcept {
        const mister::SeatScope as_ui{mister::SeatTag::Ui};
        composer_.arm(front_end);
        composer_.begin({});
    }

    void pass_on_edge() noexcept {
        if (const auto b = wire_.bound_core(); b && (!armed_ || b->gen.v != gen_.v)) {
            core_loaded(false);
            gen_ = b->gen;
            armed_ = true;
        }
        pass();
    }

    void pass() noexcept {
        const mister::SeatScope as_ui{mister::SeatTag::Ui};
        while (const auto ev = ui_rx_.pop()) {
            if (const auto r = infra::as<proto::LinkEvent::FrameReply>(*ev)) {
                if (composer_.owns(r->gen)) {
                    composer_.on_reply(*r);
                } else if (audio_.owns_runtime(r->gen)) {
                    audio_.on_runtime_probe_reply(r->word, r->gen);
                }
            }
        }
        composer_.service();
    }

    void settle_now() noexcept {
        const mister::SeatScope as_ui{mister::SeatTag::Ui};
        const auto bound = wire_.bound_core();
        wire_.publish_prelude_level(
            app::PreludeLevel{.core = bound ? bound->gen : proto::BindGeneration{}, .settled = true}
        );
    }

    [[nodiscard]] app::PreludeStats stats() const noexcept {
        return composer_.stats_cell().sample().value;
    }

private:
    app::VideoWire& wire_;
    app::LinkRxChannel<SeatTag::Ui>& ui_rx_;
    const svc::Vfs& vfs_;
    app::AudioPump audio_{vfs_, wire_};
    PreludeClock clock_{};
    app::PreludeComposer composer_;
    bool armed_ = false;
    proto::BindGeneration gen_{};
};

}
