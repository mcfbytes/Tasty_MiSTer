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

constexpr std::uint8_t kCecRegRxHeader[3] = {0x15, 0x27, 0x38};
constexpr std::uint8_t kCecRegRxLength[3] = {0x25, 0x37, 0x48};
constexpr std::uint8_t kCecRegRxStatus = 0x26;
constexpr std::uint8_t kCecRegRxReady = 0x49;
constexpr std::uint8_t kCecRegRxBuffers = 0x4A;
constexpr std::uint8_t kMainRegInt1Status = 0x97;
constexpr std::uint8_t kCecIntRxRdyMask = 0x07;

}

Ex<void> CecStateMachine::poll_deadline() {
    last_.reset();

    if (state_ == CecState::Disabled || state_ == CecState::AwaitingInit) {
        return {};
    }
    if (!io_.cec || !io_.main || !io_.hdmi) {
        return err(Errc::negotiation, ERR_SITE());
    }

    if (state_ != CecState::Idle)
        return {};

    if (!io_.hdmi->hdmi_int_asserted())
        return {};

    auto ready = io_.cec->read(kCecRegRxReady);
    if (!ready)
        return err(ready.error().code, ERR_SITE(), ready.error().detail);
    const std::uint8_t rx_bits = static_cast<std::uint8_t>(*ready & kCecIntRxRdyMask);
    if (rx_bits == 0) {

        if (auto r = io_.main->write(kMainRegInt1Status, kCecIntRxRdyMask); !r) {
            return err(r.error().code, ERR_SITE(), r.error().detail);
        }
        return {};
    }

    const std::uint32_t depth = static_cast<std::uint32_t>(
        std::popcount(static_cast<unsigned>(rx_bits))
    );
    if (depth > max_rx_depth_)
        max_rx_depth_ = depth;

    auto order = io_.cec->read(kCecRegRxStatus);
    if (!order)
        return err(order.error().code, ERR_SITE(), order.error().detail);

    int selected = -1;
    int oldest = 4;
    for (int i = 0; i < kRxBuffers; ++i) {
        if (!(rx_bits & (1u << i)))
            continue;
        const int rank = static_cast<int>((static_cast<unsigned>(*order) >> (i * 2)) & 0x03u);
        if (rank > 0 && rank < oldest) {
            oldest = rank;
            selected = i;
        }
    }
    if (selected < 0) {
        for (int i = 0; i < kRxBuffers; ++i) {
            if (rx_bits & (1u << i)) {
                selected = i;
                break;
            }
        }
    }
    if (selected < 0)
        return {};

    const auto idx = static_cast<std::size_t>(selected);

    auto len_raw = io_.cec->read(kCecRegRxLength[idx]);
    if (!len_raw)
        return err(len_raw.error().code, ERR_SITE(), len_raw.error().detail);
    const std::uint8_t length = static_cast<std::uint8_t>(*len_raw & 0x1Fu);
    const std::uint8_t hdr_reg = kCecRegRxHeader[idx];

    if (length >= 1 && length <= 16) {
        CecMessage msg{};
        msg.length = length;
        auto h = io_.cec->read(hdr_reg);
        if (!h)
            return err(h.error().code, ERR_SITE(), h.error().detail);
        msg.header = *h;
        if (length > 1) {
            auto op = io_.cec->read(static_cast<std::uint8_t>(hdr_reg + 1));
            if (!op)
                return err(op.error().code, ERR_SITE(), op.error().detail);
            msg.opcode = *op;
        }
        const std::uint8_t payload = static_cast<std::uint8_t>(length > 2 ? length - 2 : 0);
        for (std::uint8_t i = 0; i < payload; ++i) {
            auto d = io_.cec->read(static_cast<std::uint8_t>(hdr_reg + 2 + i));
            if (!d)
                return err(d.error().code, ERR_SITE(), d.error().detail);
            msg.data[i] = *d;
        }
        last_ = msg;
    } else {
        ++rx_rejected_;
    }

    const std::uint8_t bit = static_cast<std::uint8_t>(1u << selected);
    if (auto r = io_.cec->write(kCecRegRxBuffers, static_cast<std::uint8_t>(0x08u | bit)); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (io_.delay) {
        io_.delay->sleep_for(std::chrono::microseconds{kReleasePulseUs});
    }
    if (auto r = io_.cec->write(kCecRegRxBuffers, 0x08); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    ++rx_released_;

    if (auto r = io_.main->write(kMainRegInt1Status, bit); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return {};
}

}
