// SPDX-License-Identifier: GPL-3.0-or-later
#include "connect_block.h"

#include <string>
#include <utility>

#include "open_verdict.h"
#include "os/wpa_ctrl.h"
#include "svc/bt_service.h"
#include "svc/ifup_starter.h"
#include "svc/wpa_conf_store.h"

namespace mister::fw {

ConnectBlock::ConnectBlock(const ConnectPaths& paths, OpenVerdict& verdict) noexcept
    : port_{channel_.sender(), channel_.viewer()},
      paths_(paths) {
    if (auto set = os::EpollSet::create(); !set) {
        verdict.refuse("T-CONNECT: EpollSet::create", set.error());
    } else if (auto fds = xthread::ParkFds::create(connect_wake_); !fds) {
        verdict.refuse("T-CONNECT: ParkFds::create", fds.error());
    } else {
        epoll_.emplace(std::move(*set));
        fds_.emplace(std::move(*fds));
    }
}

void ConnectBlock::open_seat(const os::IClock& clock, ThreadAssembly::SeatMains& mains) noexcept {
    app::ConnectChannel::Server server = channel_.server();
    if (!server)
        fatal(Error{Errc::busy, ERR_SITE(), 0}, "connect server minted twice");
    host_.emplace(
        std::move(seated(epoll_, "T-CONNECT bound past a refused open")),
        svc::WifiService{
            os::WpaCtrlOpener{},
            svc::WpaConfStore{std::string{paths_.conf}},
            svc::IfupStarter{},
            std::string{paths_.net_class}
        },
        svc::BtService{std::string{paths_.bus_socket}},
        app::ConnectHost::Wiring{.server = std::move(server), .clock = clock}
    );
    main_.emplace(*host_, std::move(seated(fds_, "T-CONNECT bound past a refused open")));
    mains.bind(&*main_);
}

}
