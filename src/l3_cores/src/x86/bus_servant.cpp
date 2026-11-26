// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/bus_servant.h"

#include <algorithm>
#include <array>
#include <limits>

#include "infra/prelude.h"
#include "proto/uio_opcodes.h"
#include "svc/vfs.h"

namespace mister::cores::x86 {

namespace {

constexpr BusDevice kFirstLook = BusDevice::Cdda;

constexpr std::array<BusDevice, 3> kServiceOrder{BusDevice::Ide0, BusDevice::Ide1, BusDevice::Fdc};
constexpr IdeUnit::Channel kFirstChannel{0};
constexpr IdeUnit::Channel kSecondChannel{1};
constexpr hal::LinkWord kCddaOut = proto::word(proto::UioOp::DmaWrite);

constexpr std::uint16_t wire_len(infra::ByteCount n) noexcept {
    return static_cast<std::uint16_t>(n.v);
}

static_assert(
    proto::kDiskActBytes <= std::numeric_limits<std::uint16_t>::max(),
    "an act's length field carries one block"
);

[[nodiscard]] constexpr bool cdda_only(proto::ReplyWord head) noexcept {
    return head.intersects(kCddaRequest) && !head.intersects(kBusRequests);
}

[[nodiscard]] constexpr BusDevice successor(BusDevice device) noexcept {
    for (std::size_t i = 0; i < kServiceOrder.size(); ++i)
        if (kServiceOrder[i] == device)
            return kServiceOrder[(i + 1) % kServiceOrder.size()];
    return kServiceOrder.front();
}

[[nodiscard]] constexpr std::optional<IdeUnit::Channel> channel_index(BusDevice device) noexcept {
    switch (device) {
        case BusDevice::Ide0:
            return kFirstChannel;
        case BusDevice::Ide1:
            return kSecondChannel;
        case BusDevice::Fdc:
        case BusDevice::Cdda:
            break;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<X86Slot> slot_of(IdeUnit unit) noexcept {
    for (const X86Slot slot : kX86Slots) {
        const std::optional<IdeUnit> named = slot.ide();
        if (named && *named == unit)
            return slot;
    }
    return std::nullopt;
}

}

BusServant::BusServant(
    infra::OptRef<const svc::Vfs> vfs,
    std::span<const svc::DiskFormatRow> formats,
    const X86Family& family,
    proto::ReplyIndex params_at,
    infra::OptRef<xthread::WakeFlag> activity,
    infra::OptRef<RemountCell> remount
) noexcept
    : floppy_(vfs, formats, family.drives, params_at, activity),
      remount_(remount) {
    static_assert(kChannels == 2, "the x86 family's RTL decodes at most two IDE channels");
    if (!family.ide)
        return;
    const X86IdeBus& bus = *family.ide;
    if (!bus.channels.empty())
        channels_[kFirstChannel].emplace(bus.channels[0], vfs, formats, bus.geometry);
    if (bus.channels.size() > 1)
        channels_[kSecondChannel].emplace(bus.channels[1], vfs, formats, bus.geometry);
}

void BusServant::publish_image_(X86Slot slot, std::string_view name) noexcept {
    if (!remount_)
        return;
    RemountAsked record{};
    record.slot = slot.block_slot();
    if (!record.path.assign(name))
        fatal(Error{Errc::contract, ERR_SITE(), 0}, "BusServant: a name past PathText");
    remount_->publish(record);
}

void BusServant::asked(
    ide::RemountTarget target,
    ide::IdeDriveSelect addressed,
    std::string_view name
) noexcept {
    TASTY_SEAT_BODY(BusServant);
    std::optional<X86Slot> slot{};
    switch (target) {
        case ide::RemountTarget::FloppyA:
            slot = kFloppyA;
            break;
        case ide::RemountTarget::FloppyB:
            slot = kFloppyB;
            break;
        case ide::RemountTarget::ThisDrive:
            if (serving_)
                slot = slot_of(IdeUnit{*serving_, addressed});
            break;
    }
    if (slot)
        publish_image_(*slot, name);
}

infra::OptRef<ide::IdeChannel> BusServant::channel_for_(BusDevice device) noexcept {
    const std::optional<IdeUnit::Channel> index = channel_index(device);
    if (!index)
        return {};
    const auto held = channels_.find(*index);
    if (!held || !*held)
        return {};
    return infra::opt_ref_of(&**held);
}

MountState BusServant::bind(proto::DiskUnit unit, std::string_view path) noexcept {
    TASTY_SEAT_BODY(BusServant);
    const auto slot = X86Slot::of(unit);
    if (!slot)
        return MountState::Failed;
    if (const auto floppy = slot->floppy())
        return floppy_.bind(*floppy, path);
    const auto channel = slot->ide();
    if (!channel)
        return MountState::Failed;
    const auto held = channels_.find(channel->channel);
    if (!held || !*held)
        return MountState::Failed;
    (*held)->configure(channel->drive, path);
    return MountState::Done;
}

proto::DiskAct BusServant::start_device_(
    BusDevice device,
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    switch (device) {
        case BusDevice::Cdda:
            return start_cdda_(block, bytes);
        case BusDevice::Fdc:
            return floppy_.start(block, bytes);
        case BusDevice::Ide0:
        case BusDevice::Ide1:
            if (const auto channel = channel_for_(device))
                return channel->start(block, bytes);
            return proto::DiskAct::none();
    }
    return proto::DiskAct::none();
}

proto::DiskAct BusServant::resume_device_(
    BusDevice device,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    switch (device) {
        case BusDevice::Cdda:
            return cdda_act_(bytes);
        case BusDevice::Fdc:
            return floppy_.resume(bytes);
        case BusDevice::Ide0:
        case BusDevice::Ide1: {
            const auto channel = channel_for_(device);
            if (!channel)
                return proto::DiskAct::none();

            serving_ = channel_index(device);
            const proto::DiskAct act = channel->resume(
                bytes,
                infra::OptRef<ide::IRemountAsk>{*this}
            );
            serving_.reset();
            return act;
        }
    }
    return proto::DiskAct::none();
}

proto::DiskAct BusServant::cdda_act_(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    const std::optional<ide::CddaEgress::Piece> piece = egress_.next();
    if (!piece)
        return proto::DiskAct::none();
    std::ranges::copy(piece->bytes, bytes.begin());
    return proto::DiskAct::out(kCddaOut, wire_len(infra::ByteCount{piece->bytes.size()}))
        .at(kCddaPort);
}

proto::DiskAct BusServant::start_cdda_(
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    if (!block.reply(proto::ReplyIndex{}).intersects(kCddaRequest))
        return proto::DiskAct::none();
    for (std::optional<ide::IdeChannel>& held : channels_) {
        if (!held)
            continue;
        const infra::OptRef<ide::AtapiDrive> drive = held->playing_drive();
        if (drive && egress_.stage(*drive))
            return cdda_act_(bytes);
    }
    return proto::DiskAct::none();
}

proto::DiskAct BusServant::start(
    const proto::user_io::PollFrame& block,
    std::span<std::byte, proto::kDiskActBytes> bytes
) noexcept {
    TASTY_SEAT_BODY(BusServant);

    const proto::DiskAct first = start_device_(kFirstLook, block, bytes);
    if (first.kind() != proto::DiskAct::Kind::None) {
        open_ = kFirstLook;
        return first;
    }

    if (cdda_only(block.reply(proto::ReplyIndex{})))
        return proto::DiskAct::none();
    BusDevice device = last_ ? successor(*last_) : kServiceOrder.front();
    for (std::size_t i = 0; i < kServiceOrder.size(); ++i) {
        const proto::DiskAct act = start_device_(device, block, bytes);
        if (act.kind() != proto::DiskAct::Kind::None) {
            last_ = device;
            open_ = device;
            return act;
        }
        device = successor(device);
    }
    return proto::DiskAct::none();
}

proto::DiskAct BusServant::resume(std::span<std::byte, proto::kDiskActBytes> bytes) noexcept {
    TASTY_SEAT_BODY(BusServant);
    if (!open_)
        return proto::DiskAct::none();
    const proto::DiskAct act = resume_device_(*open_, bytes);
    if (act.kind() == proto::DiskAct::Kind::None)
        open_.reset();
    return act;
}

void BusServant::release() noexcept {
    TASTY_SEAT_BODY(BusServant);
    floppy_.release();
    for (std::optional<ide::IdeChannel>& channel : channels_)
        if (channel)
            channel->release();
    egress_.clear();
    last_.reset();
    open_.reset();
}

}
