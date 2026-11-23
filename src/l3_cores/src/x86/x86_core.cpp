// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_core.h"

#include <cstring>

#include "infra/byte_view.h"
#include "infra/prelude.h"
#include "proto/disk_protocol.h"
#include "proto/link/link.h"

namespace mister::cores::x86 {

X86Core::X86Core(const CoreProfile& p, const HostServices& h, const X86Family& family)
    : Core(p, h),
      family_(family) {}

Ex<void> X86Core::do_adopt(const CoreAdopt&) {
    if (!bus_port_())
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    return {};
}

infra::OptRef<IDiskPort> X86Core::bus_port_() const noexcept {
    return host().disk[proto::DiskProtocol::IsaBus];
}

void X86Core::service_kick() noexcept {
    TASTY_SEAT_BODY(X86Core);
    if (const auto port = bus_port_()) {
        proto::link::Link link{host().link};
        port->service_rt(link, family_.poll);
    }
}

MountState X86Core::mount_slot(proto::SlotIndex slot, std::string_view path) {
    TASTY_SEAT_BODY(X86Core);
    const auto media = X86Slot::of(slot.io_index());
    if (!media)
        return MountState::Failed;
    const auto drive = media->floppy();
    if (!drive || !family_.drives.contains(*drive))
        return MountState::Failed;
    const auto port = bus_port_();
    if (!port)
        return MountState::Failed;
    bool& out = bind_out_[*drive];
    if (!out || cfg_.path(*media) != path) {
        cfg_.name(*media, path);
        out = true;
        port->order_bind(*drive, path);
    }
    const MountState answer = port->bind_answer(*drive);
    if (answer == MountState::Pending)
        return MountState::Pending;
    out = false;
    return answer;
}

Ex<void> X86Core::on_mount(IoIndex slot, const MountedPath& p) {
    const auto media = X86Slot::of(slot);
    if (!media)
        return std::unexpected(slot.refusal(Errc::slot_range, ERR_SITE()));
    cfg_.name(*media, p.path);
    if (family_.declares(*media)) {
        if (const auto port = bus_port_())
            port->order_bind(slot.served_unit(), p.path);
    }
    return {};
}

Ex<void> X86Core::apply(ConfigBlob value) {
    TASTY_SEAT_BODY(X86Core);
    X86SysConfig next{};
    if (!value.bytes.empty()) {
        if (value.bytes.size() != infra::bytes_of(next).size()) {
            return std::unexpected(
                proto::FileSize::of(value.bytes).refusal(Errc::bad_format, ERR_SITE())
            );
        }
        std::memcpy(&next, value.bytes.data(), sizeof next);
    }
    cfg_ = next;
    return {};
}

std::span<const std::byte> X86Core::value(std::span<std::byte> dst) const noexcept {
    TASTY_SEAT_BODY(X86Core);
    if (dst.size() < infra::bytes_of(cfg_).size())
        return {};
    std::memcpy(dst.data(), &cfg_, sizeof cfg_);
    return dst.first(sizeof cfg_);
}

Ex<proto::SessionParams> X86Core::stream_opening(const StreamAsk& ask) {
    TASTY_SEAT_BODY(X86Core);
    return stream_.stream_opening(ask);
}

void X86Core::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(X86Core);
    stream_.stream_closed(end);
}

}
