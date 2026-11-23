// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "app/input/pad_level_sink.h"
#include "app/lw_block_endpoint.h"
#include "app/pad_grant.h"
#include "app/pad_relay_cell.h"
#include "app/pad_route.h"
#include "hal/pad_block.h"
#include "hal/reg_port.h"
#include "hal/window_port.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_owner_input.h"
#include "infra/tally.h"
#include "proto/link/axi_pad_encoder.h"
#include "proto/link/pad_levels.h"
#include "proto/pad_masks.h"
#include "svc/types.h"

namespace mister::app::input {

class LwPadEndpoint final : public LwBlockEndpoint<PadGrant, PadRoute, seat_owner::Input>,
                            public IPadLevelSink {
    TASTY_SEAT_RESIDENT(Input);
    static_assert(
        seat_owner::Input::kSeatDecl == kSeatDecl,
        "the base's owner tag and this endpoint's declaration must agree"
    );

public:
    using Port = hal::IRegPort<hal::PadBlock>;

    struct Wiring {
        const PadGrantCell& grant;
        PadRouteCell& route;
        PadLeaseCell& lease;
        const PadRelayCell& relay;
        infra::OptRef<Port> port{};
    };

    explicit LwPadEndpoint(const Wiring& w) noexcept
        : LwBlockEndpoint<PadGrant, PadRoute, seat_owner::Input>(w.grant, w.lease),
          w_(w) {}

    [[nodiscard]] bool emit(svc::PlayerIndex player, proto::PadMasks masks) noexcept override;
    [[nodiscard]] bool release(svc::PlayerIndex player, svc::JoyMask tested) noexcept override;

    [[nodiscard]] bool stick(
        svc::PlayerIndex player,
        proto::StickSide side,
        proto::StickPos pos
    ) noexcept override;

    [[nodiscard]] infra::Tally bind_count() const noexcept { return tally_.binds; }

    [[nodiscard]] infra::Tally gate_end_count() const noexcept { return gate_ends_; }

    [[nodiscard]] const PadRoute& route() const noexcept { return tally_; }

    [[nodiscard]] std::optional<proto::PadWord> wanted(
        proto::link::PadLevels::Slot s
    ) const noexcept;

private:
    using Base = LwBlockEndpoint<PadGrant, PadRoute, seat_owner::Input>;
    static_assert(
        std::is_same_v<PadLeaseCell, Base::LeaseCell>,
        "the route's lease cell is the base's lease cell"
    );
    void publish_hook_(const PadRoute& r) noexcept override;
    void prepare_hook_(const PadGrant& g) noexcept override;
    [[nodiscard]] bool arm_hook_(const PadGrant& g) noexcept override;
    void bind_hook_(const PadGrant& g) noexcept override;
    void bound_hook_() noexcept override;
    void answer_hook_(proto::BuildGeneration) noexcept override;
    void unbind_hook_() noexcept override;
    void serve_bound_() noexcept override;
    [[nodiscard]] bool idle_bound_hook_() const noexcept override;

    [[nodiscard]] bool apply_relay_(bool binding) noexcept;

    void store_() noexcept;

    void set_level_(svc::PlayerIndex player, svc::JoyMask composed) noexcept;

    void store_stick_(svc::PlayerIndex port, proto::StickSide side, proto::StickPos pos) noexcept;
    [[nodiscard]] Port& port_() noexcept;

    Wiring w_;
    std::optional<hal::WindowPort<hal::PadBlock, seat_owner::Input>> block_{};
    std::optional<proto::link::AxiPadEncoder> pads_{};

    infra::IndexedArray<infra::Tally, proto::link::PadLevels::kCount, proto::link::PadLevels::Slot>
        seen_{};
    bool gated_ = false;
    infra::Tally gate_ends_{};
    bool unpublished_ = false;
    infra::Tally refused_before_{};
    infra::Tally stores_before_{};
};

}
