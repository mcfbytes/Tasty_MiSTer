// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "app/config_rel.h"
#include "cores/save_image.h"
#include "cores/save_servant.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/save_act.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class FlatSaveServant final : public cores::ISaveServant {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kImageCap = 1024u * 1024u;

    explicit FlatSaveServant(const svc::Vfs& vfs);

    [[nodiscard]] proto::SaveAct start(
        const cores::SaveImage& image,
        std::string_view rel,
        std::uint8_t word_bytes
    ) noexcept override;
    [[nodiscard]] proto::SaveAct resume(
        std::span<const std::uint8_t, proto::kSavePieceBytes> bytes
    ) noexcept override;

private:
    enum class Phase : std::uint8_t { Idle, Opening, Reading, Closing };
    [[nodiscard]] proto::SaveAct next_read_() noexcept;

    const svc::Vfs& vfs_;
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
