// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "app/conf_str_cell.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "proto/link_event.h"
#include "proto/types.h"

namespace mister::app {

class InfoDecoder {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit InfoDecoder(const ConfStrCell& cell) noexcept : cell_(cell) {}

    InfoDecoder(const InfoDecoder&) = delete;
    InfoDecoder& operator=(const InfoDecoder&) = delete;

    [[nodiscard]] std::string_view decode(const proto::LinkEvent::PolledWord& w);

    [[nodiscard]] std::uint32_t decoded() const noexcept { return decoded_; }

    [[nodiscard]] std::uint32_t stale() const noexcept { return stale_; }

    [[nodiscard]] std::uint32_t unmapped() const noexcept { return unmapped_; }

private:
    void refresh_();
    [[nodiscard]] std::string_view info_text_(const proto::LinkEvent::PolledWord& w);

    const ConfStrCell& cell_;
    ConfStrCell::Reader reader_{};
    ConfStrText scratch_{};
    proto::ItemTable table_{};
    std::optional<std::size_t> row_{};
    proto::BindGeneration gen_{};
    std::uint32_t decoded_ = 0;
    std::uint32_t stale_ = 0;
    std::uint32_t unmapped_ = 0;
};

}
