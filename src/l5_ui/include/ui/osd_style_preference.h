// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::ui {

class OsdStylePreference final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit OsdStylePreference(bool hd, std::uint8_t theme = 0) noexcept
        : hd_(hd),
          theme_(theme) {}

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

    void boot_theme(std::uint8_t theme) noexcept { theme_ = theme; }

    std::uint8_t theme() const noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        return theme_;
    }

    void choose_theme(std::uint8_t theme) noexcept {
        TASTY_SEAT_BODY(OsdStylePreference);
        theme_ = theme;
        ++theme_choices_;
    }

    std::uint32_t classic_choices() const noexcept { return classic_choices_; }

    std::uint32_t hd_choices() const noexcept { return hd_choices_; }

    std::uint32_t theme_choices() const noexcept { return theme_choices_; }

private:
    bool hd_;
    std::uint8_t theme_;
    bool reopen_ = false;
    std::uint32_t classic_choices_ = 0;
    std::uint32_t hd_choices_ = 0;
    std::uint32_t theme_choices_ = 0;
};

}
