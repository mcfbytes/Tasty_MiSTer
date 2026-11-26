// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/pad_entries.h"
#include "proto/link/pad_key_image.h"
#include "proto/link/pad_mouse_stores.h"
#include "proto/ps2_frame.h"

namespace mister::proto::link {

class PadEvent {
public:
    PadEvent() = default;

    [[nodiscard]] static std::optional<PadEvent> of(const Ps2Frame& f) noexcept;

    [[nodiscard]] bool mouse() const noexcept { return mouse_.has_value(); }

    [[nodiscard]] hal::PadEntries entries() const noexcept {
        return mouse_ ? PadMouseStores::kEntries : PadKeyImage::kEntries;
    }

    [[nodiscard]] const std::optional<PadMouseStores>& mouse_stores() const noexcept {
        return mouse_;
    }

    [[nodiscard]] PadKeyImage key_image() const noexcept { return key_; }

private:
    explicit PadEvent(PadKeyImage k) noexcept : key_(k) {}

    explicit PadEvent(PadMouseStores m) noexcept : mouse_(m) {}

    PadKeyImage key_{};
    std::optional<PadMouseStores> mouse_{};
};

}
