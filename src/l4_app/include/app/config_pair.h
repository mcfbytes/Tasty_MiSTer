// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/link_op.h"
#include "svc/config_snapshot.h"

namespace mister::app {

class ConfigPair {
    TASTY_SEAT_RESIDENT(RT);

public:
    class IStdoutRoute {
    public:
        [[nodiscard]] virtual bool stdout_armed() const noexcept = 0;
        [[nodiscard]] virtual Ex<void> route_stdout(std::uint8_t debug) = 0;

    protected:
        IStdoutRoute() = default;
        IStdoutRoute(const IStdoutRoute&) = default;
        IStdoutRoute& operator=(const IStdoutRoute&) = default;
        ~IStdoutRoute() = default;
    };

    struct Out {
        std::optional<Errc> advise{};
        bool dropped = false;
    };

    using Cell = xthread::Telemetry<svc::ConfigSnapshot, SeatTag::Main>;

    ConfigPair(const Cell& cell, IStdoutRoute& stdout_route)
        : config_cell_(cell),
          stdout_(stdout_route) {
        config_store_[0] = std::make_unique<svc::ConfigSnapshot>();
        config_store_[1] = std::make_unique<svc::ConfigSnapshot>();
    }

    ConfigPair(const ConfigPair&) = delete;
    ConfigPair& operator=(const ConfigPair&) = delete;

    [[nodiscard]] Ex<void> apply(
        const svc::ConfigSnapshot& previous,
        const svc::ConfigSnapshot& next
    ) {
        TASTY_SEAT_BODY(ConfigPair);
        (void)previous;
        if (stdout_.stdout_armed()) {
            if (auto r = stdout_.route_stdout(next.debug); !r)
                return r;
        }
        return {};
    }

    [[nodiscard]] Out bind() noexcept {
        TASTY_SEAT_BODY(ConfigPair);
        return take_bound_config_();
    }

    [[nodiscard]] const svc::ConfigSnapshot* config() const noexcept {
        return config_store_[config_live_].get();
    }

private:
    [[nodiscard]] Out take_bound_config_() noexcept {
        if (config_reader_.take_if_changed(config_cell_, spare_config_())) {
            if (auto r = apply(live_config_(), spare_config_()); r) {
                promote_spare_();
            } else {
                return Out{.advise = r.error().code};
            }
        } else if (
            config_reader_.seen() != 0 && config_cell_.generation() == config_reader_.seen()
        ) {
            if (auto r = apply(live_config_(), live_config_()); !r) {
                return Out{.advise = r.error().code};
            }
        } else {

            return Out{.dropped = true};
        }
        return Out{};
    }

    [[nodiscard]] svc::ConfigSnapshot& live_config_() noexcept {
        return *config_store_[config_live_];
    }

    [[nodiscard]] svc::ConfigSnapshot& spare_config_() noexcept {
        return *config_store_[1u - config_live_];
    }

    void promote_spare_() noexcept { config_live_ = static_cast<std::uint8_t>(1u - config_live_); }

    const Cell& config_cell_;
    IStdoutRoute& stdout_;
    Cell::Reader config_reader_{};

    std::unique_ptr<svc::ConfigSnapshot> config_store_[2];

    std::uint8_t config_live_ = 0;
};

static_assert(
    2u * sizeof(svc::ConfigSnapshot) < 192u * 1024u,
    "rt-rule: the config store is a FIXED two slots, sized by the option table "
    "alone. Nothing here grows with the number of cores, INI sections or loads, "
    "and two snapshots is the whole of it."
);

}
