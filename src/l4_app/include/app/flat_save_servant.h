// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "app/config_rel.h"
#include "cores/nvram_save.h"
#include "cores/save_servant.h"
#include "infra/fixed_str.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/save_act.h"
#include "proto/types.h"

namespace mister::svc {
class SaveStageKeeper;
}

namespace mister::app {

class FlatSaveServant final : public cores::ISaveServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr proto::FileSize kImageCap{1024u * 1024u};

    explicit FlatSaveServant(svc::SaveStageKeeper& keeper);

    [[nodiscard]] proto::SaveAct start(
        const cores::mra::NvramSave& image,
        std::string_view rel,
        hal::Width word
    ) noexcept override;
    [[nodiscard]] proto::SaveAct resume(
        std::span<const std::uint8_t, proto::kSavePieceBytes> bytes
    ) noexcept override;

private:
    enum class Phase : std::uint8_t { Idle, Opening, Reading, Closing };
    [[nodiscard]] proto::SaveAct next_read_() noexcept;

    svc::SaveStageKeeper& keeper_;
    std::vector<std::byte> image_;
    FixedStr<kConfigRelCap, StrFit::Reject> rel_{};
    proto::WideIoIndex index_{};
    std::size_t size_ = 0;
    std::size_t at_ = 0;
    std::uint16_t piece_ = 0;
    std::uint16_t asked_ = 0;
    Phase phase_ = Phase::Idle;
};

}
