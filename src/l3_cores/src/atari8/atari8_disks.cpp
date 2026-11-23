// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/atari8_disks.h"

#include <array>
#include <utility>

#include "infra/message_sum.h"
#include "svc/vfs.h"

namespace mister::cores::atari8 {

void Atari8Disks::begin_core() noexcept {
    TASTY_SEAT_BODY(Atari8Disks);
    out_.reset();
    sio_.begin_core();
    pbi_.begin_core();
    xex_.close();
    drives_.close_all();
}

void Atari8Disks::reset() noexcept {
    TASTY_SEAT_BODY(Atari8Disks);
    out_.reset();
    drives_.close_all();
    xex_.close();
    pbi_.begin_core();
    sio_.order_reset();
}

void Atari8Disks::warm_booted() noexcept {
    TASTY_SEAT_BODY(Atari8Disks);
    xex_.close();
}

void Atari8Disks::on_rx(SioRxStatus status) noexcept {
    TASTY_SEAT_BODY(Atari8Disks);
    sio_.on_rx(status);
}

void Atari8Disks::mount(SioSlot slot, std::string_view path, Atari8Status status) {
    TASTY_SEAT_BODY(Atari8Disks);
    const std::optional<SioUnit> unit = SioDrives::unit_of(slot);
    if (unit) {
        drives_.mount(*unit, vfs_, path, status);
        if (slot == SioSlot::BootD1 && !path.empty())
            sio_.order_boot();
        return;
    }

    if (path.empty()) {
        xex_.close();
        return;
    }
    auto file = vfs_.open(path, svc::OpenMode::Read);
    if (!file) {
        xex_.close();
        return;
    }
    xex_.open(std::move(*file));
}

std::optional<Atari8Disks::ColdBoot> Atari8Disks::take_cold_boot() noexcept {
    TASTY_SEAT_BODY(Atari8Disks);
    if (xex_.rebooting())
        xex_.booted();
    if (xex_.wants_boot()) {
        xex_.boot_ordered();
        return ColdBoot::Held;
    }
    if (sio_.wants_reboot()) {
        sio_.reboot_ordered();
        return ColdBoot::Plain;
    }
    return std::nullopt;
}

SioTerms Atari8Disks::terms_(Atari8Status status) noexcept {
    return SioTerms{.drives = drives_, .status = status, .now = clock_.now()};
}

proto::MailboxAct Atari8Disks::sent_(Part part, const proto::MailboxAct& act) noexcept {
    if (infra::kind_of(act) != proto::MailboxAct::Kind::None)
        out_ = part;
    return act;
}

proto::MailboxAct Atari8Disks::next(ActBytes bytes, Atari8Status status) {
    TASTY_SEAT_BODY(Atari8Disks);
    if (out_)
        return {};
    const SioTerms t = terms_(status);
    if (xex_.arming())
        return sent_(Part::Xex, xex_.next(bytes));
    const proto::MailboxAct serial = sent_(Part::Sio, sio_.next(bytes, t));
    if (out_)
        return serial;
    return standing_(bytes, t);
}

proto::MailboxAct Atari8Disks::standing_(ActBytes bytes, const SioTerms& t) {
    const Part first = pbi_next_ ? Part::Pbi : Part::Xex;
    const std::array<Part, 2> turn{first, first == Part::Pbi ? Part::Xex : Part::Pbi};
    for (const Part part : turn) {
        pbi_next_ = !pbi_next_;
        const proto::MailboxAct act = part == Part::Pbi
            ? (pbi_.wants(t) ? sent_(Part::Pbi, pbi_.next(bytes, t)) : proto::MailboxAct{})
            : (xex_.wants() ? sent_(Part::Xex, xex_.next(bytes)) : proto::MailboxAct{});
        if (out_)
            return act;
    }
    return {};
}

bool Atari8Disks::settled(const proto::MailboxResult& result, ActBytes bytes, Atari8Status status) {
    TASTY_SEAT_BODY(Atari8Disks);
    const std::optional<Part> part = std::exchange(out_, std::nullopt);
    if (!part)
        return false;
    const SioTerms t = terms_(status);
    switch (*part) {
        case Part::Sio:
            sio_.settled(result, t);
            return false;
        case Part::Pbi:
            pbi_.settled(result, bytes, t);
            return false;
        case Part::Xex:
            xex_.settled(result, bytes);
            return xex_.take_release();
    }
    return false;
}

}
