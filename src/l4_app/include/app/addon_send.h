// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/link_tx_channel.h"
#include "app/path_text.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class AddonSend {
    TASTY_SEAT_EXEMPT(main);

public:
    enum class Pass : std::uint8_t { Waiting, Done };

    static constexpr std::uint16_t kChunk = 4096;

    struct Entry {
        std::string ext;
        proto::WideIoIndex dest{};
        PathText path{};
    };

    [[nodiscard]] static AddonSend plan(std::string_view list, std::string_view pick,
                                        std::uint8_t idx);

    [[nodiscard]] static std::optional<PathText> path_of(std::string_view pick,
                                                         std::string_view ext);

    [[nodiscard]] bool take_image(std::vector<std::uint8_t> bytes);

    [[nodiscard]] Pass step(cores::ILadderHost& host, const svc::Vfs& vfs);
    [[nodiscard]] bool wants_pass(const LinkTxChannel& inbox) const noexcept;

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::uint32_t sent() const noexcept { return sent_; }
    [[nodiscard]] std::uint32_t missing() const noexcept { return missing_; }
    [[nodiscard]] std::uint32_t failed() const noexcept { return failed_; }

    [[nodiscard]] bool image_sent() const noexcept { return image_sent_; }
    [[nodiscard]] bool image_failed() const noexcept { return image_failed_; }

private:
    static constexpr std::size_t kNoImage = static_cast<std::size_t>(-1);

    [[nodiscard]] bool open_(const svc::Vfs& vfs);

    std::vector<Entry> entries_;
    std::vector<std::uint8_t> image_;
    std::size_t image_at_ = kNoImage;
    std::size_t next_ = 0;
    std::optional<cores::PayloadPieces> pieces_{};
    std::uint32_t sent_ = 0;
    std::uint32_t missing_ = 0;
    std::uint32_t failed_ = 0;
    bool image_sent_ = false;
    bool image_failed_ = false;
};

}  // namespace mister::app
