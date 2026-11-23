// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/disk_servant.h"
#include "cores/ide/cdda_egress.h"
#include "cores/ide/ide_channel.h"
#include "cores/ide/ide_channel_row.h"
#include "cores/ide/ide_remount_ask.h"
#include "cores/remount_asked.h"
#include "cores/x86/x86_bus_poll.h"
#include "cores/x86/x86_family.h"
#include "cores/x86/x86_floppy_servant.h"
#include "cores/x86/x86_slot.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/wake_flag.h"
#include "proto/disk_act.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"
#include "svc/disk_format_row.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores::x86 {

enum class BusDevice : std::uint8_t { Fdc, Ide0, Ide1, Cdda };

class BusServant final : public IDiskServant, public ide::IRemountAsk {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kChannels = 2;

    BusServant(
        infra::OptRef<const svc::Vfs> vfs,
        std::span<const svc::DiskFormatRow> formats,
        const X86Family& family,
        proto::ReplyIndex params_at,
        infra::OptRef<xthread::WakeFlag> activity = {},
        infra::OptRef<RemountCell> remount = {}
    ) noexcept;

    BusServant(const BusServant&) = delete;
    BusServant& operator=(const BusServant&) = delete;
    BusServant(BusServant&&) = delete;
    BusServant& operator=(BusServant&&) = delete;

    [[nodiscard]] MountState bind(proto::DiskUnit unit, std::string_view path) noexcept override;
    [[nodiscard]] proto::DiskAct start(
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    [[nodiscard]] proto::DiskAct resume(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept override;
    void release() noexcept override;

    void asked(
        ide::RemountTarget target,
        ide::IdeDriveSelect addressed,
        std::string_view name
    ) noexcept override;

    [[nodiscard]] const FloppyServant& floppy() const noexcept { return floppy_; }

    [[nodiscard]] bool cdda_pending() const noexcept { return egress_.pending(); }

    [[nodiscard]] const std::optional<ide::IdeChannel>& channel_of(
        IdeUnit::Channel at
    ) const noexcept {
        return channels_[at];
    }

private:
    [[nodiscard]] proto::DiskAct start_device_(
        BusDevice device,
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    [[nodiscard]] proto::DiskAct resume_device_(
        BusDevice device,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    [[nodiscard]] infra::OptRef<ide::IdeChannel> channel_for_(BusDevice device) noexcept;

    [[nodiscard]] proto::DiskAct start_cdda_(
        const proto::user_io::PollFrame& block,
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;
    [[nodiscard]] proto::DiskAct cdda_act_(
        std::span<std::byte, proto::kDiskActBytes> bytes
    ) noexcept;

    void publish_image_(X86Slot slot, std::string_view name) noexcept;

    FloppyServant floppy_;
    infra::IndexedArray<std::optional<ide::IdeChannel>, kChannels, IdeUnit::Channel> channels_{};
    ide::CddaEgress egress_{};
    infra::OptRef<RemountCell> remount_;

    std::optional<IdeUnit::Channel> serving_{};
    std::optional<BusDevice> last_{};
    std::optional<BusDevice> open_{};
};

}
