// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "app/config_rel.h"
#include "cores/nvram_save.h"
#include "cores/save_frame.h"
#include "cores/save_servant.h"
#include "cores/save_shape.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/save_act.h"
#include "proto/types.h"
#include "svc/blank_bank_filter.h"

namespace mister::svc {
class SaveStageKeeper;
}

namespace mister::app {

class FlatSaveServant final : public cores::ISaveServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static proto::FileSize image_cap() noexcept;

    explicit FlatSaveServant(svc::SaveStageKeeper& keeper);

    [[nodiscard]] proto::SaveAct start(
        const cores::mra::NvramSave& image,
        std::string_view rel,
        hal::Width word,
        const cores::SaveShape& shape
    ) noexcept override;
    [[nodiscard]] proto::SaveAct resume(
        std::span<const std::uint8_t, proto::kSavePieceBytes> bytes
    ) noexcept override;

private:
    enum class Phase : std::uint8_t { Idle, Opening, Reading, Closing };
    [[nodiscard]] proto::SaveAct next_read_() noexcept;

    [[nodiscard]] std::span<const std::byte> filtered_() noexcept;

    svc::SaveStageKeeper& keeper_;
    std::vector<std::byte> image_;
    FixedStr<kConfigRelCap, StrFit::Reject> rel_{};
    proto::WideIoIndex index_{};
    proto::FileSize wire_{};
    proto::FileOffset at_{};
    proto::FileSize whole_{};
    cores::SaveFrame frame_{};
    svc::BlankBankFilter blank_banks_{};
    std::uint16_t piece_ = 0;
    std::uint16_t asked_ = 0;
    Phase phase_ = Phase::Idle;
};

}
