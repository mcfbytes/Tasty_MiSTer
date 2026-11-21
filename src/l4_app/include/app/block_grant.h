// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <type_traits>

#include "hal/reg_lend.h"
#include "proto/build_generation.h"

namespace mister::app {

template <class B>
concept GrantBody = std::is_trivially_copyable_v<B> && requires(const B& b) {
    { b.live() } noexcept -> std::same_as<bool>;
};

template <GrantBody B>
struct BlockGrant {
    using Body = B;
    hal::reg::Lend block{};
    proto::BuildGeneration gen{};
    [[no_unique_address]] Body body{};

    [[nodiscard]] bool live() const noexcept { return block.lent() && body.live(); }
};

template <class G>
concept BlockGrantRecord = std::derived_from<G, BlockGrant<typename G::Body>>
    && std::is_trivially_copyable_v<G>;

}
