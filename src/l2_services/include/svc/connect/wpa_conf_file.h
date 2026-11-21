// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/connect/country_code.h"
#include "os/connect/passphrase.h"
#include "os/connect/ssid.h"
#include "os/connect/wifi_security.h"

namespace mister::svc::connect {

class WpaConfFileNetwork {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] const os::connect::Ssid& ssid() const noexcept { return ssid_; }

    [[nodiscard]] std::span<const std::string> lines() const noexcept { return lines_; }

private:
    friend class WpaConfFile;

    WpaConfFileNetwork(os::connect::Ssid ssid, std::vector<std::string> lines)
        : ssid_(ssid),
          lines_(std::move(lines)) {}

    os::connect::Ssid ssid_{};
    std::vector<std::string> lines_;
};

class WpaConfFile {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<WpaConfFile> parse(std::string_view text);

    [[nodiscard]] Ex<void> add(
        os::connect::Ssid ssid,
        os::connect::WifiSecurity security,
        const os::connect::Passphrase* passphrase,
        bool hidden
    );
    void remove(os::connect::Ssid ssid);
    void set_country(os::connect::CountryCode country);

    [[nodiscard]] std::string text() const;

    [[nodiscard]] std::span<const WpaConfFileNetwork> networks() const noexcept {
        return networks_;
    }

    [[nodiscard]] std::optional<os::connect::CountryCode> country() const noexcept;

private:
    struct Place {
        std::size_t v = 0;

        [[nodiscard]] constexpr std::size_t index() const noexcept {
            const std::size_t at = v;
            return at;
        }

        friend constexpr bool operator==(Place, Place) = default;
    };

    struct Item {
        Place at{};
        bool network = false;

        [[nodiscard]] static constexpr Item of_line(Place at) noexcept { return Item{at, false}; }

        [[nodiscard]] static constexpr Item of_block(Place at) noexcept { return Item{at, true}; }

        [[nodiscard]] constexpr bool is_block() const noexcept { return network; }
    };

    [[nodiscard]] const std::string& line_at(Place place) const noexcept { return lines_[place.v]; }

    [[nodiscard]] std::string& line_at(Place place) noexcept { return lines_[place.v]; }

    [[nodiscard]] const WpaConfFileNetwork& block_at(Place place) const noexcept {
        return networks_[place.v];
    }

    std::vector<std::string> lines_;
    std::vector<WpaConfFileNetwork> networks_;
    std::vector<Item> items_;
    bool trailing_newline_ = true;
};

}
