// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bind_tail.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "app/core_builder.h"
#include "app/doorbell_lines.h"
#include "app/intern_exact.h"
#include "app/link_tx_reader.h"
#include "app/owner_push.h"
#include "cores/registry.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

void BindTail::identity(const proto::ConfStr& conf) {
    TASTY_SEAT_BODY(BindTail);
    constexpr std::size_t kCap = kLinkTxSlabBytes;
    std::string_view f[4] =
        {conf.name(), conf.button_list(0), conf.button_list(1), conf.button_list(2)};
    auto total = [&f]() noexcept {
        return f[0].size() + f[1].size() + f[2].size() + f[3].size() + 3;
    };

    for (int i = 3; i >= 1 && total() > kCap; --i)
        f[i] = {};
    if (total() > kCap)
        f[0] = f[0].substr(0, kCap - 3);
    std::string blob;
    for (unsigned i = 0; i < 4; ++i) {
        blob.append(f[i]);
        if (i != 3)
            blob.push_back('\0');
    }
    const auto id = inbox_.intern(infra::text_bytes<const std::uint8_t>(blob));
    if (!id) {
        ++tally_.identity_intern_refusals;
        return;
    }
    (void)push_gating(
        inbox_,
        tally_,
        sleeper_,
        infra::make<proto::LinkOp>(proto::LinkOp::BindIdentity{
            .blob = *id,
            .declares_cheats = conf.declares_cheats(),
            .declares_turbo = conf.declares_turbo(),
            .declares_info = conf.declares_info()
        })
    );
}

void BindTail::doorbells(
    const proto::ConfStr& conf,
    std::span<const proto::AxiLwDecl> lw,
    std::optional<std::uint32_t> owned
) {
    TASTY_SEAT_BODY(BindTail);

    std::array<proto::IrqBinding, kMaxDoorbellRows> irqs{};
    const std::size_t declared = CoreBuilder::rt_rows(conf.irq_bindings(), lw, irqs, owned);

    const std::size_t rows = std::min(declared, kMaxDoorbellRows);
    proto::LinkOp::BindDoorbells op{
        .declared = static_cast<std::uint8_t>(std::min<std::size_t>(declared, 0xFF))
    };
    if (rows != 0) {
        std::array<std::uint8_t, kLinkTxSlabBytes> packed{};
        std::memcpy(packed.data(), irqs.data(), rows * sizeof(proto::IrqBinding));
        const auto id = inbox_.intern(
            std::span<const std::uint8_t>(packed.data(), rows * sizeof(proto::IrqBinding))
        );
        if (id) {
            op.table = *id;
            op.rows = static_cast<std::uint8_t>(rows);
        } else {
            ++tally_.doorbell_intern_refusals;
        }
    }
    (void)push_gating(inbox_, tally_, sleeper_, infra::make<proto::LinkOp>(op));
}

void BindTail::joy(std::string_view core_name) noexcept {
    TASTY_SEAT_BODY(BindTail);

    (void)push_gating(
        inbox_,
        tally_,
        sleeper_,
        infra::make<proto::LinkOp>(proto::LinkOp::BindJoy{
            .suppress_analog_followup = cores::profile_for(core_name).suppress_analog_followup
        })
    );
}

void BindTail::facts(const MraFacts& facts) noexcept {
    TASTY_SEAT_BODY(BindTail);
    proto::LinkOp::BindFacts op{
        .is_arcade = facts.is_arcade,
        .vertical = facts.vertical,
        .setname_same_dir = facts.setname_same_dir,
        .rotation = facts.rotation
    };
    if (!facts.setname.empty()) {
        const auto setname = infra::text_bytes<const std::uint8_t>(facts.setname);
        const auto id = inbox_.intern(setname);
        if (id)
            op.setname = *id;
    }

    (void)push_gating(inbox_, tally_, sleeper_, infra::make<proto::LinkOp>(op));
}

void BindTail::uart(std::string_view core_name, const proto::ConfStr* conf) {
    TASTY_SEAT_BODY(BindTail);
    proto::LinkOp::BindUart op{.probe_usb_ser = true};
    struct ::stat st{};
    op.usb_ser = ::stat("/dev/ttyUSB0", &st) == 0;
    if (conf != nullptr) {
        std::string_view uart_raw, midi_raw;
        for (const auto& cap : conf->capabilities()) {
            if (cap.kind == proto::Capability::Kind::Uart) {
                op.capable = true;
                uart_raw = cap.raw;
            } else if (cap.kind == proto::Capability::Kind::Midi) {
                midi_raw = cap.raw;
            }
        }
        if (!uart_raw.empty() || !midi_raw.empty()) {
            std::string blob(uart_raw);
            blob.push_back('\0');
            blob.append(midi_raw);
            if (blob.size() <= kLinkTxSlabBytes) {
                const auto id = inbox_.intern(infra::text_bytes<const std::uint8_t>(blob));
                if (id)
                    op.tokens = *id;
            }
        }
    }
    if (!core_name.empty()) {
        std::string rel = "config/uartmode.";
        rel += core_name;
        op.mode = intern_exact(vfs_, inbox_, rel, 4);
        rel = "config/uartspeed.";
        rel += core_name;
        op.speeds = intern_exact(vfs_, inbox_, rel, 12);
    }

    (void)push_gating(inbox_, tally_, sleeper_, infra::make<proto::LinkOp>(op));
}

bool BindTail::reset_level(bool asserted) {
    TASTY_SEAT_BODY(BindTail);
    return push_gating(
        inbox_,
        tally_,
        sleeper_,
        infra::make<proto::LinkOp>(proto::LinkOp::WriteStatus{
            .start = proto::StatusBit{0},
            .width = 1,
            .value = asserted ? 1u : 0u
        })
    );
}

}
