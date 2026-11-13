// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>

#include "app/lw_grant_stamp.h"
#include "app/lw_lend_key.h"
#include "app/types.h"
#include "infra/error.h"
#include "infra/grant_lease.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"

namespace mister::app {

template <class B>
concept LwBlockRecord = requires(B b, const B c) {
    typename B::Grant;
    { B::kHasEnable } -> std::convertible_to<bool>;
    { c.lend() } noexcept -> std::same_as<typename B::Grant>;
    { b.disable() } noexcept;
    { c.stamp } -> std::convertible_to<LwGrantStamp>;
};

template <class C, class Grant>
concept LwLendCells = requires(C c, const C k) {
    c.grant.publish(Grant{});
    { xthread::held_after_revoke(k.lease) } -> std::same_as<bool>;
};

template <class C>
concept LwWakingCells = requires(C c) {
    { c.wake } -> std::convertible_to<infra::OptRef<xthread::WakeFlag>>;
};

template <class C>
concept LwRoutedCells = requires(const C c) { c.route.sample().value.bind_gen; };

template <LwBlockRecord Block, class Cells>
    requires LwLendCells<Cells, typename Block::Grant>
class LwBlockLend {
    TASTY_SEAT_RESIDENT(Main);

public:
    using Grant = typename Block::Grant;

    explicit LwBlockLend(std::optional<Cells> cells) noexcept : cells_(std::move(cells)) {}

    LwBlockLend(const LwBlockLend&) = delete;
    LwBlockLend& operator=(const LwBlockLend&) = delete;

    [[nodiscard]] bool wired() const noexcept { return cells_.has_value(); }

    [[nodiscard]] infra::OptRef<const Block> held() const noexcept {
        if (!held_)
            return infra::OptRef<const Block>{};
        return infra::OptRef<const Block>{*held_};
    }

    [[nodiscard]] bool mapped() const noexcept { return held_.has_value(); }

    [[nodiscard]] bool abandoned() const noexcept { return held_ && held_->stamp.lost(); }

    [[nodiscard]] bool built_as(proto::BuildGeneration g) const noexcept {
        return held_ && held_->stamp.names(g);
    }

    [[nodiscard]] bool unbuilt() const noexcept { return held_ && held_->stamp.unbuilt(); }

    [[nodiscard]] proto::BuildGeneration lent() const noexcept { return lent_; }

    void install(LwLendKey, Block block, LwGrantStamp stamp) noexcept {
        if (held_)
            fatal(Error{Errc::bridge_state, ERR_SITE(), 0}, "LwBlockLend::install");
        held_.emplace(std::move(block));
        held_->stamp = stamp;
    }

    [[nodiscard]] bool adopt_claim(LwLendKey, proto::BuildGeneration build) noexcept {
        if (!held_ || !held_->stamp.unbuilt())
            return false;
        held_->stamp = held_->stamp.adopted_by(build);
        return true;
    }

    void raise(proto::BuildGeneration current) noexcept {
        if (!cells_)
            return;
        lent_ = {};

        if (!held_ || !held_->stamp.lendable_for(current)) {
            if (!revoked_)
                cells_->grant.publish(Grant{});
            revoked_ = true;
            return;
        }
        cells_->grant.publish(held_->lend());

        if constexpr (LwWakingCells<Cells>) {
            if (cells_->wake)
                cells_->wake->request();
        }
        revoked_ = false;
        lent_ = held_->stamp.gen();
    }

    [[nodiscard]] bool retire(bool fabric_ok) noexcept
        requires(Block::kHasEnable)
    {
        return retire_(fabric_ok);
    }

    [[nodiscard]] bool retire() noexcept
        requires(!Block::kHasEnable)
    {
        return retire_(true);
    }

    [[nodiscard]] bool retire_on(FabricGeneration now) noexcept
        requires(Block::kHasEnable)
    {
        return retire_(held_ && held_->stamp.granted_on(now));
    }

    void abandon() noexcept {
        if (!held_)
            return;
        revoke_();
        held_->stamp = held_->stamp.with_fabric_lost();
    }

    [[nodiscard]] auto answered() noexcept
        requires LwRoutedCells<Cells>
    {
        using Route = std::remove_cvref_t<decltype(cells_->route.sample().value)>;
        if (!cells_ || !lent_.built() || judged_ == lent_)
            return std::optional<Route>{};
        const auto s = cells_->route.sample();
        if (!s || s.value.bind_gen != lent_)
            return std::optional<Route>{};
        judged_ = lent_;
        return std::optional<Route>{s.value};
    }

private:
    [[nodiscard]] bool retire_(bool fabric_ok) noexcept {
        if (!held_)
            return true;
        revoke_();
        if (leased_())
            return false;

        if (fabric_ok && !held_->stamp.lost())
            held_->disable();
        held_.reset();
        return true;
    }

    void revoke_() noexcept {

        lent_ = {};
        if (revoked_ || !cells_)
            return;
        cells_->grant.publish(Grant{});
        revoked_ = true;
    }

    [[nodiscard]] bool leased_() const noexcept {
        return cells_ && xthread::held_after_revoke(cells_->lease);
    }

    std::optional<Cells> cells_;
    std::optional<Block> held_{};
    bool revoked_ = true;
    proto::BuildGeneration lent_{};
    proto::BuildGeneration judged_{};
};

}
