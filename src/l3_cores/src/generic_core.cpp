// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/generic_core.h"

#include <array>
#include <memory>

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

Ex<void> GenericCore::do_adopt(const CoreAdopt&) {

    return {};
}

Ex<void> GenericCore::on_mount(IoIndex slot, const MountedPath& p) {

    return stream_.mount(slot, p);
}

std::unique_ptr<Core> make_generic(const CoreProfile& p, const CoreGrant& g) {
    return std::make_unique<GenericCore>(p, g.services);
}

}
