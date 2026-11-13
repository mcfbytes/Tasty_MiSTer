// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_ipl_pass.h"

#include <cstring>

#include "cores/manifests/n64.h"
#include "cores/n64_ipl3.h"
#include "cores/n64_pass_kind.h"
#include "cores/n64dd_geometry.h"

namespace mister::cores::n64 {

bool IplPass::plan_row(TransferRow& row, std::string_view path, bool boot) {
    auto f = vfs_.open(path, svc::OpenMode::Read);
    const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!size
        || !size->within(proto::FileSize{n64dd::kIplMinBytes}, proto::FileSize{n64dd::kIplBytes})) {
        return false;
    }
    if (path.data() != ipl_path_.view().data() && !ipl_path_.assign(path))
        return false;
    row.dest = RowDest::Window;
    row.shaped = true;
    row.shape = shape_key_of(PassKind::Ipl);
    row.source = ipl_path_.view();
    row.file_len = *size;
    row.extent = proto::FileSize{n64dd::kIplBytes};
    row.fill = 0;
    row.mirror = boot ? manifests::kRomLoadAddr : 0u;
    ipl_boot_ = boot;
    return true;
}

void IplPass::shape(
    const TransferRow&,
    LoadFacts& slab,
    std::span<std::uint8_t> piece,
    PiecePlace at
) {
    TASTY_SEAT_BODY(IplPass);
    shape_ipl_(slab, piece, at);
}

void IplPass::shape_ipl_(LoadFacts& slab, std::span<std::uint8_t> piece, PiecePlace at) {
    if (at == PiecePlace::First) {
        ipl_rule_ = word_order_for_(detect_byte_order(piece));
        slab.kind = ipl_boot_ ? FactsKind::IplBoot : FactsKind::IplPreload;
        slab.edit = proto::StatusEdit::seeded(slab.edit.bits);
        slab.crc32 = {};
        slab.bytes = proto::FileSize{};
    }
    normalize_(piece, ipl_rule_);
    if (at != PiecePlace::First || piece.size() < kMinRomBytes)
        return;
    static constexpr std::string_view kDevId = "NDXJ";
    const bool dev = std::memcmp(piece.data() + kCartIdOffset, kDevId.data(), kDevId.size()) == 0;
    slab.set_flag(opt::kDdDevelopment, dev);
    if (!ipl_boot_)
        return;
    const auto sums = bootcode_sum_.sums(piece.first(kMinRomBytes));
    const auto d = detect_from_bootcode(static_cast<char>(piece[kCartIdRegionOffset]), sums);
    if (slab.options().auto_detect()) {
        slab.set_system(d.system);
        slab.set_cic(d.cic);
        slab.set_cart_save_type(MemoryType::None);
    }
}

infra::OptRef<const IWordOrder> IplPass::word_order_for_(ByteOrder order) noexcept {
    switch (order) {
        case ByteOrder::Big:
        case ByteOrder::Unknown:
            break;
        case ByteOrder::ByteSwapped:
            return infra::OptRef<const IWordOrder>{halfword_swap_};
        case ByteOrder::Little:
            return infra::OptRef<const IWordOrder>{word_reverse_};
    }
    return {};
}

void IplPass::normalize_(
    std::span<std::uint8_t> data,
    infra::OptRef<const IWordOrder> rule
) noexcept {
    if (rule)
        rule->normalize(data);
}

void IplPass::remember_boot_(std::string_view path) noexcept {
    if (!path.empty())
        (void)memo_.last_boot.assign(path);
}

Ex<bool> IplPass::finish(LoadFacts&, infra::OptRef<ICoreWindow>) {
    TASTY_SEAT_BODY(IplPass);
    if (ipl_boot_)
        remember_boot_(ipl_path_.view());
    return true;
}

}
