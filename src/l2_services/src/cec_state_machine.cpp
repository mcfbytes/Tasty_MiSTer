// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/cec_state_machine.h"
#include "svc/adv7513_reg_map.h"

#include "hal/hdmi_int.h"
#include "os/delay.h"

#include <bit>
#include <chrono>

#include "video_service_detail.h"

namespace mister::svc {

namespace {

struct CecRxBuffer {
    os::I2cReg header;
    os::I2cReg length;
};

constexpr CecRxBuffer kRxRows[] = {
    {os::I2cReg{0x15}, os::I2cReg{0x25}},
    {os::I2cReg{0x27}, os::I2cReg{0x37}},
    {os::I2cReg{0x38}, os::I2cReg{0x48}},
};
static_assert(std::size(kRxRows) == CecStateMachine::kRxBuffers, "one row per ADV7513 RX buffer");

constexpr os::I2cReg kCecRegRxStatus{0x26};
constexpr os::I2cReg kCecRegRxReady{0x49};
constexpr os::I2cReg kCecRegRxBuffers{0x4A};
constexpr os::I2cReg kMainRegInt1Status{0x97};
constexpr std::uint8_t kCecIntRxRdyMask = 0x07;

constexpr std::uint8_t kRxReleaseBracket = 0x08;

struct RxSlot {
    static constexpr unsigned kRankBits = 2;
    std::uint8_t v = 0;

    [[nodiscard]] constexpr const CecRxBuffer& regs() const noexcept { return kRxRows[v]; }

    [[nodiscard]] constexpr std::uint8_t bit() const noexcept {
        return static_cast<std::uint8_t>(1u << v);
    }

    [[nodiscard]] constexpr std::uint8_t release_byte() const noexcept {
        return static_cast<std::uint8_t>(kRxReleaseBracket | bit());
    }

    [[nodiscard]] constexpr unsigned rank_shift() const noexcept { return unsigned{v} * kRankBits; }
};

struct RxReady {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr RxReady of(std::uint8_t reg) noexcept {
        return RxReady{static_cast<std::uint8_t>(reg & kCecIntRxRdyMask)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool holds(RxSlot slot) const noexcept { return (v & slot.bit()) != 0; }

    [[nodiscard]] constexpr std::uint32_t depth() const noexcept {
        return static_cast<std::uint32_t>(std::popcount(v));
    }
};

struct RxOrder {
    static constexpr unsigned kRankMask = 0x03;
    static constexpr unsigned kNoRank = 4;
    std::uint8_t v = 0;

    [[nodiscard]] constexpr unsigned rank_of(RxSlot slot) const noexcept {
        return (unsigned{v} >> slot.rank_shift()) & kRankMask;
    }

    [[nodiscard]] constexpr std::optional<RxSlot> oldest_ready(RxReady ready) const noexcept {
        std::optional<RxSlot> selected;
        unsigned oldest = kNoRank;
        for (std::uint8_t i = 0; i < CecStateMachine::kRxBuffers; ++i) {
            const RxSlot slot{i};
            if (!ready.holds(slot))
                continue;
            const unsigned rank = rank_of(slot);
            if (rank > 0 && rank < oldest) {
                oldest = rank;
                selected = slot;
            }
        }
        if (selected)
            return selected;
        for (std::uint8_t i = 0; i < CecStateMachine::kRxBuffers; ++i) {
            if (ready.holds(RxSlot{i}))
                return RxSlot{i};
        }
        return std::nullopt;
    }
};

[[nodiscard]] Ex<void> release_rx(const adv7513::Io& io, RxSlot slot) {
    if (auto r = io.cec->write(kCecRegRxBuffers, slot.release_byte()); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (io.delay) {
        io.delay->sleep_for(CecStateMachine::kReleasePulse);
    }
    if (auto r = io.cec->write(kCecRegRxBuffers, kRxReleaseBracket); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return {};
}

}

Ex<CecMessage> CecStateMachine::decode_rx(
    const adv7513::Io& io,
    os::I2cReg hdr_reg,
    CecLength length
) {
    CecMessage msg{};
    msg.length = length;
    auto h = io.cec->read(hdr_reg);
    if (!h)
        return err(h.error().code, ERR_SITE(), h.error().detail);
    msg.header = *h;
    if (length.has_opcode()) {
        auto op = io.cec->read(hdr_reg.plus(1));
        if (!op)
            return err(op.error().code, ERR_SITE(), op.error().detail);
        msg.opcode = *op;
    }
    const std::span<std::uint8_t> payload = length.payload_of(msg.data);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        auto d = io.cec->read(hdr_reg.plus(CecLength::kHeaderBytes + i));
        if (!d)
            return err(d.error().code, ERR_SITE(), d.error().detail);
        payload[i] = *d;
    }
    return msg;
}

Ex<void> CecStateMachine::poll_deadline(const adv7513::Io& io) {
    last_.reset();

    if (state_ == CecState::Disabled || state_ == CecState::AwaitingInit) {
        return {};
    }
    if (!io.cec || !io.main || !io.hdmi) {
        return err(Errc::negotiation, ERR_SITE());
    }

    if (state_ != CecState::Idle)
        return {};

    if (!io.hdmi->hdmi_int_asserted())
        return {};

    auto ready_reg = io.cec->read(kCecRegRxReady);
    if (!ready_reg)
        return err(ready_reg.error().code, ERR_SITE(), ready_reg.error().detail);
    const RxReady ready = RxReady::of(*ready_reg);
    if (ready.empty()) {

        if (auto r = io.main->write(kMainRegInt1Status, kCecIntRxRdyMask); !r) {
            return err(r.error().code, ERR_SITE(), r.error().detail);
        }
        return {};
    }

    const std::uint32_t depth = ready.depth();
    if (depth > max_rx_depth_)
        max_rx_depth_ = depth;

    auto order = io.cec->read(kCecRegRxStatus);
    if (!order)
        return err(order.error().code, ERR_SITE(), order.error().detail);

    const std::optional<RxSlot> selected = RxOrder{*order}.oldest_ready(ready);
    if (!selected)
        return {};
    const RxSlot slot = *selected;

    auto len_raw = io.cec->read(slot.regs().length);
    if (!len_raw)
        return err(len_raw.error().code, ERR_SITE(), len_raw.error().detail);
    if (const auto length = CecLength::of(*len_raw)) {
        auto msg = decode_rx(io, slot.regs().header, *length);
        if (!msg)
            return std::unexpected(msg.error());
        last_ = *msg;
    } else {
        ++rx_rejected_;
    }

    if (auto r = release_rx(io, slot); !r)
        return r;
    ++rx_released_;

    if (auto r = io.main->write(kMainRegInt1Status, slot.bit()); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return {};
}

}
