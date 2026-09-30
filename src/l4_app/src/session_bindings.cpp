// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_bindings.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace mister::app {

namespace {

std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(b[off]) | (static_cast<std::uint32_t>(b[off + 1]) << 8) |
           (static_cast<std::uint32_t>(b[off + 2]) << 16) |
           (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

}  // namespace

void SessionBindings::bind_facts(const proto::LinkOp::BindFacts& op,
                                 const LinkTxChannel* inbox) noexcept {
    facts_ = MraFacts{};
    facts_.is_arcade = op.is_arcade;
    facts_.vertical = op.vertical;
    facts_.setname_same_dir = op.setname_same_dir;
    facts_.rotation = op.rotation;
    if (op.setname != proto::TxSlabId{} && inbox != nullptr) {
        const auto bytes = inbox->bytes(op.setname);
        facts_.setname.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
}

void SessionBindings::bind_identity(const proto::LinkOp::BindIdentity& op,
                                    const LinkTxChannel* inbox) noexcept {
    Identity& id = identity_.emplace(Identity{});
    id.declares_cheats = op.declares_cheats;
    id.declares_turbo = op.declares_turbo;
    if (op.blob == proto::TxSlabId{} || inbox == nullptr) return;
    const auto blob = inbox->bytes(op.blob);

    FixedStr<kButtonListCap, StrFit::Clip>* const out[3] = {&id.j, &id.jn, &id.jp};
    std::size_t at = 0;
    for (unsigned f = 0; f < 4 && at <= blob.size(); ++f) {
        std::size_t end = at;
        while (end < blob.size() && blob[end] != 0)
            ++end;
        const std::string_view v(reinterpret_cast<const char*>(blob.data()) + at, end - at);
        if (f == 0) {
            (void)id.core_name.assign(v);
        } else {
            (void)out[f - 1]->assign(v);
        }
        at = end + 1;
    }
}

void SessionBindings::bind_uart(const proto::LinkOp::BindUart& op,
                                const LinkTxChannel* inbox) noexcept {
    uart_ = Uart{};
    if (inbox != nullptr && op.mode != proto::TxSlabId{}) {
        const auto b = inbox->bytes(op.mode);
        if (b.size() == 4) uart_.mode = le32(b, 0);
    }
    if (inbox != nullptr && op.speeds != proto::TxSlabId{}) {
        const auto b = inbox->bytes(op.speeds);
        if (b.size() == 12)
            uart_.speeds = std::array<std::uint32_t, 3>{le32(b, 0), le32(b, 4), le32(b, 8)};
    }

    uart_.capable = op.capable;
    if (inbox != nullptr && op.tokens != proto::TxSlabId{}) {
        const auto blob = inbox->bytes(op.tokens);
        const std::string_view v(reinterpret_cast<const char*>(blob.data()), blob.size());
        const auto sep = v.find('\0');
        (void)uart_.uart_token.assign(v.substr(0, sep));
        if (sep != std::string_view::npos) (void)uart_.midi_token.assign(v.substr(sep + 1));
    }
}

void SessionBindings::bind_doorbells(const proto::LinkOp::BindDoorbells& op,
                                     const LinkTxChannel* inbox) noexcept {
    doorbells_ = Doorbells{};
    doorbells_.declared = op.declared;
    if (op.table == proto::TxSlabId{} || inbox == nullptr) return;
    const auto bytes = inbox->bytes(op.table);
    const std::size_t n = std::min(bytes.size() / sizeof(proto::IrqBinding), kDoorbellRows);
    if (n != 0) std::memcpy(doorbells_.rows.data(), bytes.data(), n * sizeof(proto::IrqBinding));
    doorbells_.count = static_cast<std::uint8_t>(n);
}

Ex<std::optional<SessionBindings::Mount>> SessionBindings::decode_mount(
    const proto::LinkOp::BindMount& op, const LinkTxChannel* inbox) noexcept {
    if (op.image == proto::FileId{} || inbox == nullptr) return std::optional<Mount>{};
    const FileBytes::Slot* const file = inbox->file(op.image);
    if (file == nullptr || file->path.empty()) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), op.image.v});
    }
    return std::optional<Mount>{Mount{file->path, file->size_bytes}};
}

}  // namespace mister::app
