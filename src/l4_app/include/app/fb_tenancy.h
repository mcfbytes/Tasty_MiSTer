// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

enum class FbTenant : std::uint8_t { Launcher, HdOsd, kCount };

enum class FbArrival : std::uint8_t { Displace, Refuse };

inline constexpr std::
    array<std::array<FbArrival, infra::ordinal(FbTenant::kCount)>, infra::ordinal(FbTenant::kCount)>
        kFbArrival{{
            {FbArrival::Displace, FbArrival::Displace},
            {FbArrival::Refuse, FbArrival::Displace},
        }};

static_assert(
    kFbArrival[infra::ordinal(FbTenant::Launcher)][infra::ordinal(FbTenant::Launcher)]
            == FbArrival::Displace
        && kFbArrival[infra::ordinal(FbTenant::HdOsd)][infra::ordinal(FbTenant::HdOsd)]
            == FbArrival::Displace,
    "kFbArrival: a tenant re-raising itself displaces its own earlier claim"
);

static_assert(
    kFbArrival[infra::ordinal(FbTenant::HdOsd)][infra::ordinal(FbTenant::Launcher)]
            == FbArrival::Refuse
        && kFbArrival[infra::ordinal(FbTenant::Launcher)][infra::ordinal(FbTenant::HdOsd)]
            == FbArrival::Displace,
    "kFbArrival: the launcher outranks the HD OSD, never the reverse"
);

enum class FbShow : std::uint8_t { Full, View };

struct FbGen {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr FbGen first() noexcept { return FbGen{1}; }

    [[nodiscard]] constexpr FbGen next() const noexcept { return FbGen{v + 1}; }

    friend constexpr bool operator==(FbGen, FbGen) = default;
};

class FbTenancy {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Lease {
        FbTenant who;
        FbGen gen;

        friend constexpr bool operator==(Lease, Lease) = default;
    };

    [[nodiscard]] Ex<Lease> claim(FbTenant who, FbShow show = FbShow::Full) noexcept;

    [[nodiscard]] bool holds(const Lease& lease) const noexcept;

    void release(const Lease& lease) noexcept;

    void hold(bool on) noexcept;

    [[nodiscard]] bool held() const noexcept { return held_; }

    [[nodiscard]] bool claimed() const noexcept { return current_.has_value(); }

    [[nodiscard]] bool viewing() const noexcept;

private:
    std::optional<Lease> current_{};
    FbGen next_gen_ = FbGen::first();
    bool held_ = false;
    bool viewing_ = false;
};

using FbLease = FbTenancy::Lease;

[[nodiscard]] inline Ex<FbLease> FbTenancy::claim(FbTenant who, FbShow show) noexcept {
    TASTY_SEAT_BODY(FbTenancy);
    if (current_
        && kFbArrival[infra::ordinal(who)][infra::ordinal(current_->who)] == FbArrival::Refuse)
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    current_ = Lease{who, next_gen_};
    next_gen_ = next_gen_.next();
    viewing_ = show == FbShow::View;
    return *current_;
}

inline bool FbTenancy::holds(const FbLease& lease) const noexcept {
    return current_.has_value() && *current_ == lease;
}

inline void FbTenancy::release(const FbLease& lease) noexcept {
    TASTY_SEAT_BODY(FbTenancy);
    if (!holds(lease))
        return;
    current_.reset();
    viewing_ = false;
}

inline void FbTenancy::hold(bool on) noexcept {
    TASTY_SEAT_BODY(FbTenancy);
    held_ = on;
}

inline bool FbTenancy::viewing() const noexcept {
    return current_.has_value() && viewing_;
}

}
