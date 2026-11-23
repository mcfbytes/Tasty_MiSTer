// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/minimig_core.h"

#include <cstring>

#include "cores/core_adopt.h"
#include "cores/minimig/floppy_poll.h"
#include "cores/minimig/minimig_fact.h"
#include "cores/mounted_path.h"
#include "infra/byte_view.h"
#include "proto/disk_protocol.h"
#include "proto/link/link.h"

namespace mister::cores::minimig {

Ex<void> MinimigCore::do_adopt(const CoreAdopt&) {
    TASTY_SEAT_BODY(MinimigCore);
    if (!floppy_port_())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    return {};
}

infra::OptRef<IDiskPort> MinimigCore::floppy_port_() const noexcept {
    return host().disk[proto::DiskProtocol::AmigaFloppy];
}

void MinimigCore::service_kick() noexcept {
    TASTY_SEAT_BODY(MinimigCore);
    if (const auto port = floppy_port_()) {
        proto::link::Link link{host().link};
        port->service_rt(link, kFloppyPoll);
    }
}

MountState MinimigCore::mount_slot(proto::SlotIndex slot, std::string_view path) {
    TASTY_SEAT_BODY(MinimigCore);
    const std::optional<proto::DiskUnit> drive = floppy_of_(slot);
    const infra::OptRef<IDiskPort> port = floppy_port_();
    if (!drive || !port)
        return MountState::Failed;
    const proto::DiskUnit unit = *drive;
    const auto found = floppies_.find(unit);
    if (!found)
        return MountState::Failed;
    FloppyBind& b = *found;
    if (!b.out || b.path.view() != path) {
        (void)b.path.assign(path);
        b.out = true;
        b.bound = false;
        port->order_bind(unit, path);
    }
    const MountState answer = port->bind_answer(unit);
    if (answer == MountState::Pending)
        return MountState::Pending;
    b.out = false;
    b.bound = answer == MountState::Done && !path.empty();
    return answer;
}

std::optional<proto::DiskUnit> MinimigCore::floppy_of_(proto::SlotIndex slot) noexcept {
    for (std::uint8_t u = 0; u < kFloppyDrives; ++u) {
        if (proto::DiskUnit{u}.slot() == slot)
            return proto::DiskUnit{u};
    }
    return std::nullopt;
}

bool MinimigCore::floppy_bound(proto::DiskUnit unit) const noexcept {
    const auto found = floppies_.find(unit);
    return found && found->bound;
}

Ex<void> MinimigCore::on_apply_load_facts(std::span<const std::uint8_t> facts, std::string_view) {
    TASTY_SEAT_BODY(MinimigCore);
    for (const std::uint8_t fact : facts) {
        switch (fact_of(fact)) {
            case MinimigFact::DetachFloppies:
                detach_floppies_();
                break;
            case MinimigFact::kCount:
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), fact});
        }
    }
    return {};
}

void MinimigCore::detach_floppies_() noexcept {
    const infra::OptRef<IDiskPort> port = floppy_port_();
    for (std::uint8_t u = 0; u < kFloppyDrives; ++u) {
        const proto::DiskUnit unit{u};
        floppies_[unit] = FloppyBind{};
        if (!port)
            continue;
        port->abandon_command_on(unit);
        port->order_bind(unit, {});
    }
}

Ex<void> MinimigCore::apply(ConfigBlob value) {
    TASTY_SEAT_BODY(MinimigCore);
    if (value.bytes.empty()) {
        cfg_ = MinimigConfig::defaults();
        return {};
    }
    if (value.bytes.size() != cfg_.image.size()) {
        return std::unexpected(
            proto::FileSize::of(value.bytes).refusal(Errc::bad_format, ERR_SITE())
        );
    }
    std::memcpy(cfg_.image.data(), value.bytes.data(), cfg_.image.size());
    return {};
}

std::span<const std::byte> MinimigCore::value(std::span<std::byte> dst) const noexcept {
    TASTY_SEAT_BODY(MinimigCore);
    if (dst.size() < cfg_.image.size())
        return {};
    std::memcpy(dst.data(), cfg_.image.data(), cfg_.image.size());
    return dst.first(cfg_.image.size());
}

Ex<void> MinimigCore::on_mount(proto::IoIndex slot, const MountedPath& image) {
    TASTY_SEAT_BODY(MinimigCore);
    if (slot == kKickstartSlot) {
        cfg_.set_kickstart(image.path);
    } else if (slot == kExtRomSlot) {
        cfg_.set_ext_rom(image.path);
    } else {

        return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
    }
    return {};
}

}
