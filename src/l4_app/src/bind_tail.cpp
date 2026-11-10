// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bind_tail.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>

#include "app/core_builder.h"
#include "app/intern_exact.h"
#include "app/link_tx_reader.h"
#include "app/session_bindings.h"
#include "app/owner_push.h"
#include "cores/registry.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

std::array<std::string_view, SessionBindings::kIdentityFields> bind_identity_fields(
    const proto::ConfStr& conf
) noexcept {
    constexpr std::size_t kCap = kLinkTxSlabBytes;

    constexpr std::size_t kSeparators = SessionBindings::kIdentityFields - 1;
    std::array<std::string_view, SessionBindings::kIdentityFields> f =
        {conf.name(), conf.button_list(0), conf.button_list(1), conf.button_list(2)};
    auto total = [&f]() noexcept {
        std::size_t n = kSeparators;
        for (const std::string_view v : f)
            n += v.size();
        return n;
    };

    for (std::size_t i = kSeparators; i >= 1 && total() > kCap; --i)
        f[i] = {};
    if (total() > kCap)
        f[0] = f[0].substr(0, kCap - kSeparators);
    return f;
}

bool BindTail::identity(const proto::ConfStr& conf) {
    TASTY_SEAT_BODY(BindTail);
    const std::array<std::string_view, SessionBindings::kIdentityFields> f = bind_identity_fields(
        conf
    );
    std::string blob;
    for (std::size_t i = 0; i < f.size(); ++i) {
        blob.append(f[i]);
        if (i + 1 != f.size())
            blob.push_back('\0');
    }
    const auto id = inbox_.intern(infra::text_bytes<const std::uint8_t>(blob));
    if (!id) {
        ++tally_.identity_intern_refusals;
        return false;
    }
    return push_gating(
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
    std::optional<hal::LwOffset> owned
) {
    TASTY_SEAT_BODY(BindTail);

    const CoreBuilder::RtRows rt = CoreBuilder::rt_rows(conf.irq_bindings(), lw, owned);
    proto::LinkOp::BindDoorbells op{.declared = rt.declared.wire_byte()};

    if (!rt.count.empty()) {
        std::array<std::uint8_t, kLinkTxSlabBytes> packed{};
        const std::size_t bytes = rt.count.bytes_of<proto::IrqBinding>();
        std::memcpy(packed.data(), rt.rows.data(), bytes);
        const auto id = inbox_.intern(std::span<const std::uint8_t>(packed.data(), bytes));
        if (id) {
            op.table = *id;
            op.rows = rt.count.wire_byte();
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
        const auto setname = infra::text_bytes<const std::uint8_t>(facts.setname.view());
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
        op.mode = intern_exact(vfs_, inbox_, rel, SessionBindings::Uart::kModeBytes);
        rel = "config/uartspeed.";
        rel += core_name;
        op.speeds = intern_exact(vfs_, inbox_, rel, SessionBindings::Uart::kSpeedsBytes);
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
