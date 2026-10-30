// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "app/identity_record.h"
#include "app/link_tx_reader.h"
#include "app/mra_facts.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/irq_binding.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::app {

static_assert(std::is_trivially_copyable_v<proto::IrqBinding>);
static_assert(sizeof(proto::IrqBinding) == 8);

class SessionBindings {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kIdentityNameCap = 64;
    static constexpr std::size_t kButtonListCap = kLinkTxSlabBytes;
    static constexpr std::size_t kCapTokenCap = 96;

    static constexpr std::size_t kDoorbellRows = kLinkTxSlabBytes / sizeof(proto::IrqBinding);
    static_assert(
        MraFacts::kSetnameCap == kIdentityNameCap,
        "the setname and the latched identity name clip at one width"
    );

    struct Identity {
        FixedStr<kIdentityNameCap, StrFit::Clip> core_name{};
        FixedStr<kButtonListCap, StrFit::Clip> j{};
        FixedStr<kButtonListCap, StrFit::Clip> jn{};
        FixedStr<kButtonListCap, StrFit::Clip> jp{};
        bool declares_cheats = false;
        bool declares_turbo = false;
        bool declares_info = false;
    };

    struct Uart {
        bool capable = false;
        FixedStr<kCapTokenCap, StrFit::Clip> uart_token{};
        FixedStr<kCapTokenCap, StrFit::Clip> midi_token{};
        std::optional<std::uint32_t> mode{};
        std::optional<std::array<std::uint32_t, 3>> speeds{};
    };

    struct Doorbells {
        std::array<proto::IrqBinding, kDoorbellRows> rows{};
        std::uint8_t count = 0;
        std::uint8_t declared = 0;

        [[nodiscard]] std::span<const proto::IrqBinding> table() const noexcept {
            return {rows.data(), count};
        }
    };

    struct Mount {
        decltype(InternedFile::path) path{};
        std::uint64_t size_bytes = 0;
    };

    void bind_facts(const proto::LinkOp::BindFacts& op, const ILinkTxReader& inbox) noexcept;
    void bind_identity(const proto::LinkOp::BindIdentity& op, const ILinkTxReader& inbox) noexcept;
    void bind_uart(const proto::LinkOp::BindUart& op, const ILinkTxReader& inbox) noexcept;
    void bind_doorbells(
        const proto::LinkOp::BindDoorbells& op,
        const ILinkTxReader& inbox
    ) noexcept;

    [[nodiscard]] static Ex<std::optional<Mount>> decode_mount(
        const proto::LinkOp::BindMount& op,
        const ILinkTxReader& inbox
    ) noexcept;

    [[nodiscard]] Ex<void> bind_record(
        const IdentityRecordCell& cell,
        const proto::LinkOp::AdoptCore& op
    ) noexcept;

    void forget_core() noexcept {
        identity_.reset();
        uart_ = Uart{};
        doorbells_ = Doorbells{};
        record_ = no_record_;
    }

    void spend_uart_files() noexcept {
        uart_.mode.reset();
        uart_.speeds.reset();
    }

    void bind_generation(proto::BindGeneration g) noexcept { gen_ = g; }

    [[nodiscard]] const proto::BindGeneration& generation() const noexcept { return gen_; }

    [[nodiscard]] const MraFacts& facts() const noexcept { return facts_; }

    [[nodiscard]] const IdentityRecord& record() const noexcept { return record_; }

    [[nodiscard]] const std::optional<Identity>& identity() const noexcept { return identity_; }

    [[nodiscard]] const Uart& uart() const noexcept { return uart_; }

    [[nodiscard]] const Doorbells& doorbells() const noexcept { return doorbells_; }

    [[nodiscard]] std::string_view core_name() const noexcept {
        return identity_.has_value() ? identity_->core_name.view() : std::string_view{};
    }

    [[nodiscard]] bool declares_cheats() const noexcept {
        return identity_.has_value() && identity_->declares_cheats;
    }

    [[nodiscard]] bool declares_turbo() const noexcept {
        return identity_.has_value() && identity_->declares_turbo;
    }

    [[nodiscard]] bool declares_info() const noexcept {
        return identity_.has_value() && identity_->declares_info;
    }

private:
    MraFacts facts_{};
    std::optional<Identity> identity_{};

    IdentityRecord no_record_{build_identity_record({}, MraFacts{})};
    IdentityRecord record_{no_record_};
    Uart uart_{};
    Doorbells doorbells_{};
    proto::BindGeneration gen_{};
};

}
