// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bind_tail.h"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>

#include "app/core_builder.h"
#include "app/front_end_status.h"
#include "app/intern_exact.h"
#include "app/link_tx_reader.h"
#include "app/session_bindings.h"
#include "app/owner_push.h"
#include "app/uart_binding.h"
#include "app/uart_persist.h"
#include "cores/registry.h"
#include "infra/byte_view.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

namespace {

std::string card_path_(std::string_view stem, std::string_view core) {
    std::string rel{stem};
    rel += core;
    return rel;
}

bool usb_serial_present_() noexcept {
    struct ::stat st{};
    return ::stat("/dev/ttyUSB0", &st) == 0;
}

}

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
    UartBinding::Inputs in{};
    in.usb_ser = usb_serial_present_();
    in.default_baud = cores::profile_for(core_name).default_uart_baud;
    if (conf != nullptr) {
        for (const auto& cap : conf->capabilities()) {
            if (cap.kind == proto::Capability::Kind::Uart) {
                in.capable = true;
                in.tokens.uart = cap.raw;
            } else if (cap.kind == proto::Capability::Kind::Midi) {
                in.tokens.midi = cap.raw;
            }
        }
    }

    if (in.capable && !core_name.empty()) {
        in.files = UartBinding::Files::from_card(
            read_exact<UartPersist::kCardBytes>(vfs_, card_path_("config/uartmode.", core_name)),
            read_exact<UartBinding::SpeedsFile::kBytes>(
                vfs_,
                card_path_("config/uartspeed.", core_name)
            )
        );
    }
    const UartBinding binding = UartBinding::decode(in);
    proto::LinkOp::BindUart op{};
    if (!binding.is_inert()) {
        if (const auto id = inbox_.intern(binding.image())) {
            op.binding = *id;
        } else {
            tally_.uart_intern_refusals.add();
        }
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
            .field = proto::kResetField,
            .value = proto::FieldValue::flag(asserted)
        })
    );
}

bool BindTail::front_end_pal(bool pal) {
    TASTY_SEAT_BODY(BindTail);
    return push_gating(
        inbox_,
        tally_,
        sleeper_,
        infra::make<proto::LinkOp>(proto::LinkOp::WriteStatus{
            .field = kFrontEndPalField,
            .value = proto::FieldValue::flag(pal)
        })
    );
}

}
