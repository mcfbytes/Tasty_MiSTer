// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/archie/archie_config.h"
#include "cores/archie/archie_option.h"
#include "cores/cmos_layout.h"
#include "cores/config_blob.h"
#include "cores/config_slots.h"
#include "cores/core_adopt.h"
#include "cores/core.h"
#include "cores/host_services.h"
#include "cores/mounted_path.h"
#include "cores/nvram_save.h"
#include "cores/option_row.h"
#include "cores/option_row_view.h"
#include "cores/option_rows.h"
#include "cores/save_upload.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto::link {
class CoreSession;
}

namespace mister::cores::archie {

class ArchieCore final : public Core, public IOptionRows, public IConfigSlots, public ISaveUpload {
    TASTY_SEAT_RESIDENT(RT);

public:
    ArchieCore(const CoreProfile& p, const HostServices& h);

    static constexpr IoIndex kFloppy0Slot{0};
    static constexpr IoIndex kFloppy1Slot{1};
    static constexpr IoIndex kRomSlot{4};
    static constexpr std::size_t kFloppies = 2;

    static constexpr IoIndex kRomIndex{1};
    static constexpr IoIndex kCmosIndex{3};

    static constexpr CmosLayout kCmos{
        .bytes = infra::ByteCount{256},
        .year_at = infra::ByteOffset{0xC0},
        .century_at = infra::ByteOffset{0xC1},
        .century = std::byte{20},
        .index = proto::WideIoIndex::of(kCmosIndex)
    };
    static_assert(kCmos.well_formed());
    static constexpr std::string_view kCmosFile = "cmos.dat";

    [[nodiscard]] std::span<const OptionRow> option_rows() const noexcept override {
        TASTY_SEAT_BODY(ArchieCore);
        return profile().option_rows;
    }

    [[nodiscard]] OptionRowView option_view(proto::OptionRowIndex row) noexcept override;
    [[nodiscard]] Ex<void> set_option(proto::OptionRowIndex row, proto::FieldValue choice) override;

    [[nodiscard]] Ex<void> apply(ConfigBlob value) override;
    [[nodiscard]] std::span<const std::byte> value(
        std::span<std::byte> dst
    ) const noexcept override;

    [[nodiscard]] std::string_view save_file_name() const noexcept override {
        TASTY_SEAT_BODY(ArchieCore);
        return kCmosFile;
    }

    [[nodiscard]] std::optional<mra::NvramSave> save_image() const noexcept override;

    [[nodiscard]] std::string_view rom_path() const noexcept { return cfg_.rom_img.view(); }

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;

    [[nodiscard]] infra::OptRef<IOptionRows> on_options() noexcept override {
        return infra::OptRef<IOptionRows>{*this};
    }

    [[nodiscard]] infra::OptRef<IConfigSlots> on_config_slots() noexcept override {
        return infra::OptRef<IConfigSlots>{*this};
    }

    [[nodiscard]] infra::OptRef<ISaveUpload> on_save_upload() noexcept override {
        return infra::OptRef<ISaveUpload>{*this};
    }

    [[nodiscard]] SystemCtrl live_ctrl_() const noexcept;
    [[nodiscard]] std::string_view file_text_(ArchieOption o) const noexcept;

    infra::OptRef<proto::link::CoreSession> session_{};

    ArchieConfig cfg_{};
    std::array<ArchieConfig::Path, kFloppies> floppy_img_{};
};

}
