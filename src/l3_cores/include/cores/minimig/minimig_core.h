// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/config_blob.h"
#include "cores/config_slots.h"
#include "cores/core.h"
#include "cores/mailbox_carriage.h"
#include "cores/minimig/cd_decl.h"
#include "cores/disk_port.h"
#include "cores/mount_state.h"
#include "cores/sector_kick.h"
#include "cores/slot_mount.h"
#include "cores/minimig/io_frame_sink.h"
#include "cores/minimig/minimig_config.h"
#include "cores/minimig/minimig_options.h"
#include "cores/option_rows.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/image_sink.h"
#include "proto/types.h"

namespace mister::cores::minimig {

class MinimigCore final : public Core,
                          public IConfigSlots,
                          public IOptionRows,
                          public ISectorKick,
                          public ISlotMount {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr proto::IoIndex kKickstartSlot{4};
    static constexpr proto::IoIndex kExtRomSlot{5};

    static constexpr std::uint8_t kFloppyDrives = 4;

    static constexpr std::size_t kFloppyPathBytes = 1024;

    MinimigCore(const CoreProfile& profile, const HostServices& host)
        : Core(profile, host),
          frames_(host.link),
          mailbox_(MailboxCarriage::granted(host, cd_decl::kCdMailbox)),
          options_(profile.option_rows) {}

    [[nodiscard]] Ex<void> apply(ConfigBlob value) override;
    [[nodiscard]] std::span<const std::byte> value(
        std::span<std::byte> dst
    ) const noexcept override;

    [[nodiscard]] std::span<const OptionRow> option_rows() const noexcept override {
        TASTY_SEAT_BODY(MinimigCore);
        return profile().option_rows;
    }

    [[nodiscard]] OptionRowView option_view(proto::OptionRowIndex row) noexcept override {
        TASTY_SEAT_BODY(MinimigCore);
        return options_.view(row, cfg_);
    }

    [[nodiscard]] Ex<void> set_option(
        proto::OptionRowIndex row,
        proto::FieldValue choice
    ) override {
        TASTY_SEAT_BODY(MinimigCore);
        return options_.set(row, choice, cfg_, frames_);
    }

    [[nodiscard]] const IoFrameSink::Tally& frame_tally() const noexcept { return frames_.tally(); }

    void service_kick() noexcept override;

    [[nodiscard]] MountState mount_slot(proto::SlotIndex slot, std::string_view path) override;

    [[nodiscard]] bool floppy_bound(proto::DiskUnit unit) const noexcept;

private:
    struct FloppyBind {
        FixedStr<kFloppyPathBytes, StrFit::Clip> path{};
        bool out = false;
        bool bound = false;
    };

    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;

    [[nodiscard]] Ex<void> on_mount(proto::IoIndex slot, const MountedPath& image) override;

    [[nodiscard]] infra::OptRef<IConfigSlots> on_config_slots() noexcept override {
        return infra::OptRef<IConfigSlots>{*this};
    }

    [[nodiscard]] infra::OptRef<IOptionRows> on_options() noexcept override {
        return infra::OptRef<IOptionRows>{*this};
    }

    [[nodiscard]] proto::IImageSink& on_image_sink() noexcept override { return frames_; }

    [[nodiscard]] infra::OptRef<IMailboxRows> on_mailbox_rows() noexcept override {
        return mailbox_ ? infra::OptRef<IMailboxRows>{*mailbox_} : infra::OptRef<IMailboxRows>{};
    }

    [[nodiscard]] infra::OptRef<ISectorKick> on_sector_kick() noexcept override {
        return infra::OptRef<ISectorKick>{*this};
    }

    [[nodiscard]] infra::OptRef<ISlotMount> on_slot_mount() noexcept override {
        return infra::OptRef<ISlotMount>{*this};
    }

    [[nodiscard]] Ex<void> on_apply_load_facts(
        std::span<const std::uint8_t> facts,
        std::string_view image
    ) override;

    [[nodiscard]] infra::OptRef<IDiskPort> floppy_port_() const noexcept;

    [[nodiscard]] static std::optional<proto::DiskUnit> floppy_of_(proto::SlotIndex slot) noexcept;
    void detach_floppies_() noexcept;

    MinimigConfig cfg_ = MinimigConfig::defaults();
    IoFrameSink frames_;
    std::optional<MailboxCarriage> mailbox_;
    MinimigOptions options_;
    infra::IndexedArray<FloppyBind, kFloppyDrives, proto::DiskUnit> floppies_{};
};

}
