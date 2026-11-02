// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/cd_link.h"
#include "cores/cd_wire_port.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/image_sink.h"

namespace mister::cores {

class CdWireLink final : public ICdLink {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdWireLink(CdWirePort& wire, proto::IImageSink& sink) noexcept : wire_(wire), sink_(sink) {}

    ~CdWireLink() override = default;
    CdWireLink(const CdWireLink&) = delete;
    CdWireLink& operator=(const CdWireLink&) = delete;

    [[nodiscard]] Ex<void> send_data(
        IoIndex io_index,
        std::span<const std::byte> payload
    ) override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.send_data(io_index, payload, sink_);
    }

    [[nodiscard]] Ex<void> send_status(std::span<const std::uint16_t> words) override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.send_status(words);
    }

    bool can_send_data(std::uint16_t selector) override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.can_send_data(selector);
    }

    [[nodiscard]] bool egress_bound() const noexcept override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.queued();
    }

    [[nodiscard]] bool egress_idle() const noexcept override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.egress_idle();
    }

    [[nodiscard]] Ex<void> submit_sector(const CdSectorAct& act) override {
        TASTY_SEAT_BODY(CdWireLink);
        return wire_.submit_sector(act);
    }

private:
    CdWirePort& wire_;
    proto::IImageSink& sink_;
};

}
