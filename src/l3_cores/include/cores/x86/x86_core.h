// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/config_slots.h"
#include "cores/core_support.h"
#include "cores/disk_port.h"
#include "cores/mounted_path.h"
#include "cores/sector_kick.h"
#include "cores/slot_mount.h"
#include "cores/stream_load.h"
#include "cores/stream_load_host.h"
#include "cores/x86/x86_family.h"
#include "cores/x86/x86_slot.h"
#include "cores/x86/x86_sys_config.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::cores::x86 {

class X86Core final : public Core,
                      public ISectorKick,
                      public ISlotMount,
                      public IConfigSlots,
                      public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    X86Core(const CoreProfile& p, const HostServices& h, const X86Family& family);

    void service_kick() noexcept override;

    [[nodiscard]] MountState mount_slot(proto::SlotIndex slot, std::string_view path) override;

    [[nodiscard]] Ex<void> apply(ConfigBlob value) override;
    [[nodiscard]] std::span<const std::byte> value(
        std::span<std::byte> dst
    ) const noexcept override;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(const StreamAsk& ask) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] std::string_view path(X86Slot slot) const noexcept { return cfg_.path(slot); }

    [[nodiscard]] const X86Family& family() const noexcept { return family_; }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<ISectorKick> on_sector_kick() noexcept override {
        return infra::OptRef<ISectorKick>{*this};
    }

    [[nodiscard]] infra::OptRef<ISlotMount> on_slot_mount() noexcept override {
        return infra::OptRef<ISlotMount>{*this};
    }

    [[nodiscard]] infra::OptRef<IConfigSlots> on_config_slots() noexcept override {
        return infra::OptRef<IConfigSlots>{*this};
    }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{*this};
    }

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] infra::OptRef<IDiskPort> bus_port_() const noexcept;

    const X86Family& family_;
    X86SysConfig cfg_{};
    StreamLoadHost stream_;

    infra::IndexedArray<bool, X86Slot::kFloppies, proto::DiskUnit> bind_out_{};
};

}
