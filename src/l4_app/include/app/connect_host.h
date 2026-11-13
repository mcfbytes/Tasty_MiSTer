// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/connect_channel.h"
#include "app/view_mark.h"
#include "app/connect_fd_sync.h"
#include "app/connect_sink.h"
#include "infra/message_sum.h"
#include "infra/park_timeout.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/types.h"
#include "os/epoll_set.h"
#include "svc/connect_fd_delta.h"
#include "svc/view_gen.h"
#include "svc/wifi_link.h"

namespace mister::app {

template <class Wifi, class Bt>
class BasicConnectHost {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr infra::ParkTimeout kEngagedPark{250};
    static constexpr infra::ParkTimeout kIdlePark{2000};

    static constexpr std::size_t kBatch = ConnectChannel::kRequestDepth;

    struct Wiring {
        ConnectChannel::Server server;
        const os::IClock& clock;
    };

    BasicConnectHost(os::EpollSet epoll, Wifi wifi, Bt bt, Wiring w) noexcept
        : epoll_(std::move(epoll)),
          wifi_(std::move(wifi)),
          bt_(std::move(bt)),
          server_(std::move(w.server)),
          clock_(w.clock),
          sync_(epoll_),
          sink_(wifi_, bt_) {}

    BasicConnectHost(const BasicConnectHost&) = delete;
    BasicConnectHost& operator=(const BasicConnectHost&) = delete;

    [[nodiscard]] const os::EpollSet& epoll() const noexcept { return epoll_; }

    void serve() noexcept {
        const os::RawMonotonicClock::time_point now = clock_.now();
        for (std::size_t n = 0; n < kBatch; ++n) {
            const std::optional<svc::ConnectRequest> request = server_.pop();
            if (!request)
                break;
            infra::dispatch<infra::AllRouted>(*request, sink_);
            requests_.add();
        }
        sync_.apply(wifi_.tick(now), wifi_);
        sync_.apply(bt_.tick(now), bt_);
        publish_();
    }

    void stop() noexcept { bt_.stop(); }

    [[nodiscard]] bool idle() const noexcept { return server_.requests_empty(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept {
        return engaged_() ? kEngagedPark : kIdlePark;
    }

    [[nodiscard]] svc::Tally requests() const noexcept { return requests_; }

    [[nodiscard]] svc::Tally fd_add_failures() const noexcept { return sync_.add_failures(); }

    [[nodiscard]] svc::Tally misrouted() const noexcept { return sink_.misrouted_count(); }

    [[nodiscard]] const Wifi& wifi() const noexcept { return wifi_; }

    [[nodiscard]] const Bt& bt() const noexcept { return bt_; }

private:
    [[nodiscard]] bool engaged_() const noexcept {
        return sink_.wifi_page_open()
            || sink_.bt_page_open()
            || wifi_.view().link == svc::WifiLink::Associating
            || wifi_.view().link == svc::WifiLink::Handshake
            || bt_.view().activity.busy();
    }

    template <class View>
    void publish_view_(const View& view, ViewMark& mark) noexcept {
        if (mark.take(view.gen))
            server_.publish(view);
    }

    void publish_() noexcept {
        publish_view_(wifi_.view(), wifi_gen_);
        publish_view_(wifi_.scan_view(), scan_gen_);
        publish_view_(wifi_.saved_view(), saved_gen_);
        publish_view_(bt_.view(), bt_gen_);
        publish_view_(bt_.device_view(), devices_gen_);
        publish_view_(bt_.prompt_view(), prompt_gen_);
    }

    os::EpollSet epoll_;
    Wifi wifi_;
    Bt bt_;
    ConnectChannel::Server server_;
    const os::IClock& clock_;
    ConnectFdSync sync_;
    BasicConnectSink<Wifi, Bt> sink_;
    ViewMark wifi_gen_;
    ViewMark scan_gen_;
    ViewMark saved_gen_;
    ViewMark bt_gen_;
    ViewMark devices_gen_;
    ViewMark prompt_gen_;
    svc::Tally requests_{};
};

}
