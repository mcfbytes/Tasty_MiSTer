// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/hd_look.h"
#include "infra/seat.h"

namespace mister::ui {

class OsdStylePreference final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit OsdStylePreference(bool hd, app::HdLook look = {}) noexcept : hd_(hd), look_(look) {}

    bool hd() const noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        return hd_;
    }

    void choose_classic() noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        hd_ = false;
        reopen_ = true;
        ++classic_choices_;
    }

    void choose_hd() noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        hd_ = true;
        reopen_ = true;
        ++hd_choices_;
    }

    [[nodiscard]] bool take_reopen() noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        const bool r = reopen_;
        reopen_ = false;
        return r;
    }

    void boot_hd(bool hd) noexcept { hd_ = hd; }

    void boot_look(app::HdLook look) noexcept { look_ = look; }

    app::HdLook look() const noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        return look_;
    }

    void choose_look(app::HdLookField field, std::uint8_t value) noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        look_ = app::hd_look_with(look_, field, value);
        ++look_choices_;
    }

    std::uint32_t classic_choices() const noexcept { return classic_choices_; }

    std::uint32_t hd_choices() const noexcept { return hd_choices_; }

    std::uint32_t look_choices() const noexcept { return look_choices_; }

private:
    bool hd_;
    app::HdLook look_;
    bool reopen_ = false;
    std::uint32_t classic_choices_ = 0;
    std::uint32_t hd_choices_ = 0;
    std::uint32_t look_choices_ = 0;
};

}
