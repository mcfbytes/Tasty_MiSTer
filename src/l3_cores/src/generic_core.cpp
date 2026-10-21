// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/generic_core.h"

#include "cores/file_tx.h"

#include <algorithm>
#include <cstring>

namespace mister::cores {

namespace {

constexpr std::array<LinkDecoderDecl, 0> kNoServices{};
constexpr std::array<FileSlot, 0> kNoSlots{};
constexpr std::array<BootAsset, 0> kNoAssets{};
constexpr std::array<SignatureRow, 0> kNoSignatures{};

}

const CoreProfile kGenericProfile{
    .kind = CoreKind::Generic,
    .name = "",
    .rbf = "",
    .services = kNoServices,
    .slots = kNoSlots,
    .boot_assets = kNoAssets,
    .signatures = kNoSignatures,
    .undeclared_role = SlotRole::Save,
};

void GenericCore::set_pending_ext(std::string_view ext) noexcept {
    tx_.set_ext(ext);
}

Ex<void> GenericCore::do_adopt(const CoreAdopt&) {

    return {};
}

Ex<proto::SessionParams> GenericCore::stream_opening(IoIndex) {
    TASTY_SEAT_BODY(GenericCore);
    tx_.opened();
    proto::SessionParams params{};
    params.ext = tx_.ext();
    return params;
}

void GenericCore::stream_closed(const StreamLoadEnd& end) noexcept {
    TASTY_SEAT_BODY(GenericCore);
    last_index_ = static_cast<std::uint16_t>(end.index.v);
    ++transfers_;
    chunks_ += static_cast<std::uint32_t>((end.bytes + kChunkBytes - 1) / kChunkBytes);
    tx_.closed(end);
}

Ex<void> GenericCore::on_mount(IoIndex slot, const MountedPath&) {

    ++mount_refusals_;
    (void)slot;
    return unimplemented(ERR_SITE());
}

std::unique_ptr<Core> make_generic(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<GenericCore>(p, g.services);
}

}
