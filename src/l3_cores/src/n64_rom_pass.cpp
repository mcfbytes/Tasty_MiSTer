// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_rom_pass.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "cores/n64_patch_table.h"
#include "infra/byte_view.h"
#include "infra/endian.h"

namespace mister::cores::n64 {

void RomPass::begin(
    LoadFacts& slab,
    proto::FileSize size,
    const proto::StatusWord& status,
    std::string_view path
) {
    TASTY_SEAT_BODY(RomPass);
    facts_ = RomFacts{};
    facts_.bytes = size;
    ladder_.reset();
    md5_ = mra::Md5{};
    crc_.restart();
    slab = LoadFacts{};
    slab.edit = proto::StatusEdit::seeded(status);
    slab.bytes = size;
    patches_applied_ = svc::Tally{};
    patch_errors_ = 0;
    rom_pending_ = false;
    (void)rom_path_.assign(path);
}

void RomPass::shape(
    const TransferRow& row,
    LoadFacts& slab,
    std::span<std::uint8_t> piece,
    PiecePlace at
) {
    TASTY_SEAT_BODY(RomPass);
    if (at == PiecePlace::First && rom_pending_) {

        const proto::StatusWord bits = slab.edit.bits;
        begin(slab, cart_bytes_, bits, row.source);
    }
    shape_rom_(piece, at);
}

void RomPass::shape_rom_(std::span<std::uint8_t> piece, PiecePlace at) {
    if (at == PiecePlace::First) {
        facts_.order = detect_byte_order(piece);
        rom_rule_ = word_order_for_(facts_.order);
    }
    normalize_(piece, rom_rule_);
    if (at == PiecePlace::First) {
        const std::span<const std::uint8_t> head = piece.first(
            std::min(piece.size(), kHeaderBytes)
        );
        md5_.update(head);
        mra::Md5 header = md5_;
        facts_.header_md5 = header.hex_str();
        analyze_header_(head);
        md5_.update(piece.subspan(head.size()));
    } else {
        md5_.update(piece);
    }

    for (std::size_t at_byte = 0; at_byte < piece.size(); at_byte += kHeaderBytes) {
        const std::size_t n = std::min(kHeaderBytes, piece.size() - at_byte);
        std::memcpy(swap_.data(), piece.data() + at_byte, n);
        halfword_swap_.normalize(std::span<std::uint8_t>(swap_).first(n));
        crc_.extend(std::span<const std::uint8_t>(swap_).first(n));
    }
    facts_.crc32 = crc_.value();
}

infra::OptRef<const IWordOrder> RomPass::word_order_for_(ByteOrder order) noexcept {
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

void RomPass::normalize_(
    std::span<std::uint8_t> data,
    infra::OptRef<const IWordOrder> rule
) noexcept {
    if (rule)
        rule->normalize(data);
}

Ex<bool> RomPass::finish(LoadFacts& slab, infra::OptRef<ICoreWindow> window) {
    TASTY_SEAT_BODY(RomPass);
    facts_.file_md5 = md5_.hex_str();
    const DetectOutcome detected = ladder_.run(facts_, slab);
    facts_.detected = detected.detected;
    facts_.homebrew = detected.homebrew;
    apply_patches_(slab, window);
    ladder_.compose_info(facts_, slab, patches_applied_ != svc::Tally{});
    slab.crc32 = facts_.crc32;
    slab.kind = FactsKind::Rom;

    proto::StatusWord fresh = slab.edit.bits;
    if (status_cell_) {
        const proto::StatusCell::Sample sample = status_cell_->sample();
        if (sample)
            fresh = sample.value;
    }
    slab.saves = slab.save_plan(fresh);
    if (!rom_path_.empty())
        (void)memo_.last_boot.assign(rom_path_.view());
    return true;
}

void RomPass::apply_patches_(const LoadFacts& slab, infra::OptRef<ICoreWindow> window) noexcept {
    if (!window)
        return;
    if (!slab.options().patches_auto())
        return;
    const auto& match = ladder_.match();
    if (!match || !match->tags)
        return;
    const PatchTable& patches = match->tags->patches;

    constexpr std::uint32_t kWordBytes = 4;
    for (const PatchTable::Entry& e : patches.view().first(patches.applicable())) {
        const std::uint32_t at = e.address * kWordBytes;
        if (proto::FileSize{at} >= facts_.bytes)
            break;
        if (e.turbo_core_only && !turbo_)
            continue;
        std::array<std::byte, sizeof(std::uint32_t)> word{};
        if (auto r = window->read(at, word); !r) {
            ++patch_errors_;
            break;
        }
        std::array<std::byte, sizeof(std::uint32_t)> diff{};
        infra::store_le<std::uint32_t>(std::span<std::byte, sizeof(std::uint32_t)>(diff), e.diff);
        for (std::size_t i = 0; i < word.size(); ++i) {
            word[i] ^= diff[i];
        }
        if (auto r = window->write(at, word); !r) {
            ++patch_errors_;
            break;
        }
        patches_applied_.add();
    }
}

void RomPass::analyze_header_(std::span<const std::uint8_t> head) {
    if (const auto header = rom_header_of(head)) {
        facts_.internal_name = internal_name(*header);
        facts_.cart_id = cart_id(*header);

        for (std::size_t i = 0; i < kControllerCount; ++i)
            facts_.controllers[i] = (*header)[kControllerOffset + i];
    }
    facts_.bootcode = bootcode_sum_.sums(head);
}

}
