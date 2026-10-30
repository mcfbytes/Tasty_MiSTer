// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_bindings.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "infra/byte_view.h"
#include "infra/endian.h"

namespace mister::app {

namespace {

std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(b[off])
        | (static_cast<std::uint32_t>(b[off + 1]) << 8)
        | (static_cast<std::uint32_t>(b[off + 2]) << 16)
        | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

}

void SessionBindings::bind_facts(
    const proto::LinkOp::BindFacts& op,
    const ILinkTxReader& inbox
) noexcept {
    facts_ = MraFacts{};
    facts_.is_arcade = op.is_arcade;
    facts_.vertical = op.vertical;
    facts_.setname_same_dir = op.setname_same_dir;
    facts_.rotation = op.rotation;
    if (op.setname != proto::TxSlabId{}) {
        const auto bytes = inbox.bytes(op.setname);
        (void)facts_.setname.assign(infra::as_text(bytes));
    }
}

void SessionBindings::bind_identity(
    const proto::LinkOp::BindIdentity& op,
    const ILinkTxReader& inbox
) noexcept {
    Identity& id = identity_.emplace(Identity{});
    id.declares_cheats = op.declares_cheats;
    id.declares_turbo = op.declares_turbo;
    id.declares_info = op.declares_info;
    if (op.blob == proto::TxSlabId{})
        return;
    const auto blob = inbox.bytes(op.blob);

    FixedStr<kButtonListCap, StrFit::Clip>* const out[3] = {&id.j, &id.jn, &id.jp};
    std::size_t at = 0;
    for (unsigned f = 0; f < 4 && at <= blob.size(); ++f) {
        std::size_t end = at;
        while (end < blob.size() && blob[end] != 0)
            ++end;
        const std::string_view v = infra::as_text(blob.subspan(at, end - at));
        if (f == 0) {
            (void)id.core_name.assign(v);
        } else {
            (void)out[f - 1]->assign(v);
        }
        at = end + 1;
    }
}

void SessionBindings::bind_uart(
    const proto::LinkOp::BindUart& op,
    const ILinkTxReader& inbox
) noexcept {
    uart_ = Uart{};
    if (op.mode != proto::TxSlabId{}) {
        const auto b = inbox.bytes(op.mode);
        if (b.size() == 4)
            uart_.mode = le32(b, 0);
    }
    if (op.speeds != proto::TxSlabId{}) {
        const auto b = inbox.bytes(op.speeds);
        if (b.size() == 12)
            uart_.speeds = std::array<std::uint32_t, 3>{le32(b, 0), le32(b, 4), le32(b, 8)};
    }

    uart_.capable = op.capable;
    if (op.tokens != proto::TxSlabId{}) {
        const auto blob = inbox.bytes(op.tokens);
        const std::string_view v = infra::as_text(blob);
        const auto sep = v.find('\0');
        (void)uart_.uart_token.assign(v.substr(0, sep));
        if (sep != std::string_view::npos)
            (void)uart_.midi_token.assign(v.substr(sep + 1));
    }
}

void SessionBindings::bind_doorbells(
    const proto::LinkOp::BindDoorbells& op,
    const ILinkTxReader& inbox
) noexcept {
    doorbells_ = Doorbells{};
    doorbells_.declared = op.declared;
    if (op.table == proto::TxSlabId{})
        return;
    const auto bytes = inbox.bytes(op.table);
    const std::size_t n = std::min(bytes.size() / sizeof(proto::IrqBinding), kDoorbellRows);
    for (std::size_t i = 0; i < n; ++i) {
        doorbells_.rows[i] = infra::load_native<proto::IrqBinding>(
            bytes.subspan(i * sizeof(proto::IrqBinding)).first<sizeof(proto::IrqBinding)>()
        );
    }
    doorbells_.count = static_cast<std::uint8_t>(n);
}

Ex<void> SessionBindings::bind_record(
    const IdentityRecordCell& cell,
    const proto::LinkOp::AdoptCore& op
) noexcept {

    if (cell.sample_into(record_) == 0) {
        record_ = no_record_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), 0});
    }
    if (record_.stamp.v == 0 || record_.stamp != op.identity) {
        record_ = no_record_;
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), op.identity.v});
    }
    return {};
}

Ex<std::optional<SessionBindings::Mount>> SessionBindings::decode_mount(
    const proto::LinkOp::BindMount& op,
    const ILinkTxReader& inbox
) noexcept {
    if (op.image == proto::FileId{})
        return std::optional<Mount>{};
    const InternedFile* const file = inbox.file(op.image);
    if (file == nullptr || file->path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), op.image.v});
    }
    return std::optional<Mount>{Mount{file->path, file->size_bytes}};
}

}
