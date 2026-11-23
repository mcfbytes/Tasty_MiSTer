// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/disk_relays.h"

#include "infra/prelude.h"

namespace mister::app {

void DiskRelays::pump_on_caller() noexcept {

    if (sealed_) {
        sealed_pumps_.add();
        return;
    }
    const SeatScope stands_in_for_io{SeatTag::Io};
    serve();
}

cores::DiskPorts DiskRelays::ports() noexcept {
    cores::DiskPorts ports{};
    for (DiskRelay& relay : relays_)
        ports[relay.protocol()] = infra::OptRef<cores::IDiskPort>{relay};
    return ports;
}

DiskAttachSeq DiskRelays::attach(const cores::DiskServantSet& servants) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelays, A);
    order_ = order_.next();
    for (DiskRelay& relay : relays_)
        relay.attach(order_, infra::opt_ref_of(servants[relay.protocol()].get()));
    return order_;
}

DiskAttachSeq DiskRelays::detach() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelays, A);
    order_ = order_.next();
    for (DiskRelay& relay : relays_)
        relay.attach(order_, {});
    return order_;
}

bool DiskRelays::settle_attach(DiskAttachSeq order) noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelays, A);
    bool home = true;
    for (DiskRelay& relay : relays_)
        home = relay.settle_attach(order) && home;
    return home;
}

proto::FioWordCount DiskRelays::take_round_words() noexcept {
    proto::FioWordCount wrote{};
    for (DiskRelay& relay : relays_)
        wrote += relay.take_round_words();
    return wrote;
}

void DiskRelays::serve() noexcept {
    TASTY_SEAT_BODY_MEDIATOR(DiskRelays, B);
    for (DiskRelay& relay : relays_)
        relay.serve();
}

bool DiskRelays::idle() const noexcept {
    for (const DiskRelay& relay : relays_)
        if (!relay.idle())
            return false;
    return true;
}

infra::OptRef<DiskRelay> DiskRelays::relay(proto::DiskProtocol protocol) noexcept {
    for (DiskRelay& relay : relays_)
        if (relay.protocol() == protocol)
            return infra::OptRef<DiskRelay>{relay};
    return {};
}

}
