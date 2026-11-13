// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cores/core_profile.h"
#include "cores/fix_shape.h"
#include "cores/i_rom_shape.h"
#include "cores/neogeo_walk_tally.h"
#include "cores/loader.h"
#include "cores/row_shape_key.h"
#include "cores/neo_transform.h"
#include "cores/romset_file.h"
#include "cores/spr_dbl_shape.h"
#include "cores/spr_dbl_swap_shape.h"
#include "cores/spr_shape.h"
#include "cores/types.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/status_bit.h"
#include "proto/status_word.h"
#include "svc/vfs.h"

namespace mister::cores {

namespace neogeo {

inline constexpr std::uint32_t kLoRomBytes = 0x10000;
inline constexpr std::uint32_t kLoadBufBytes = 0x10'0000;

inline constexpr std::uint32_t kCdEnBit = 1u << 31;
inline constexpr std::uint32_t kFillBytes = 128u * 1024u;
inline constexpr std::uint8_t kFillPattern = 0xAA;
inline constexpr std::uint32_t kCdBiosBytes = 0x8'0000;

inline constexpr proto::StatusBit kMvsBit{1};
inline constexpr proto::StatusBit kCdzBit{2};
inline constexpr proto::StatusBit kOriginalBiosBit{22};

enum class Shape : std::uint8_t { None, Crom, Fix, SprDbl, SprDblSwap };

[[nodiscard]] constexpr RowShapeKey shape_key_of(Shape kind) noexcept {
    return RowShapeKey{std::to_underlying(kind)};
}

[[nodiscard]] constexpr std::optional<Shape> shape_of(RowShapeKey key) noexcept {
    switch (key.v) {
        case std::to_underlying(Shape::None):
            return Shape::None;
        case std::to_underlying(Shape::Crom):
            return Shape::Crom;
        case std::to_underlying(Shape::Fix):
            return Shape::Fix;
        case std::to_underlying(Shape::SprDbl):
            return Shape::SprDbl;
        case std::to_underlying(Shape::SprDblSwap):
            return Shape::SprDblSwap;
        default:
            break;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool notify_memcp(FileIndex index, bool force = false) noexcept {
    return force || !(index == FileIndex::M1 || index.is_vrom());
}

[[nodiscard]] constexpr std::uint32_t notify_size(FileIndex index, std::uint32_t size) noexcept {
    constexpr std::uint32_t kAlign = 0x10'0000u - 1u;
    return index.is_prom() ? ((size + kAlign) & ~kAlign) : size;
}

[[nodiscard]] constexpr PostNotify make_notify(
    FileIndex index,
    std::uint32_t size,
    bool force = false
) noexcept {
    return index.notify_words(notify_size(index, size), notify_memcp(index, force));
}

[[nodiscard]] constexpr PostNotify make_conf_notify(std::uint32_t conf) noexcept {
    return PostNotify::config(conf);
}

inline constexpr CoreWindowSlot kMemWindowSlot{0};
inline constexpr CoreWindowSlot kRomWindowSlot{1};

struct RomPlace {
    WindowRef window = WindowRef::at(kMemWindowSlot);
    std::uint32_t offset = 0;
};

inline constexpr std::uint32_t kM1WindowOffset = 0x200'0000u;

[[nodiscard]] constexpr RomPlace rom_place(FileIndex index, std::uint32_t concat) noexcept {
    if (concat != 0)
        return {WindowRef::at(kMemWindowSlot), concat};
    if (index.is_vrom())
        return {WindowRef::at(kRomWindowSlot), index.vrom_offset()};
    if (index == FileIndex::M1)
        return {WindowRef::at(kRomWindowSlot), kM1WindowOffset};
    return {WindowRef::at(kMemWindowSlot), 0};
}

[[nodiscard]] constexpr std::uint32_t stock_read_len(
    std::uint32_t asked,
    std::uint32_t extent,
    std::uint64_t rest
) noexcept {
    std::uint64_t n = asked;
    if (extent <= kLoadBufBytes) {
        n = asked < extent ? asked : extent;
    } else if (asked >= kLoadBufBytes) {
        n = extent;
    }
    return static_cast<std::uint32_t>(n < rest ? n : rest);
}

[[nodiscard]] std::string make_path(std::string_view root, std::string_view name);

[[nodiscard]] std::string save_path(std::string_view core, std::string_view pick);

}

class NeoGeoLoader final : public ILoader {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr CoreWindowSlot kMemWindow = neogeo::kMemWindowSlot;
    static constexpr CoreWindowSlot kRomWindow = neogeo::kRomWindowSlot;
    static constexpr std::size_t kPathChars = 1024;

    explicit NeoGeoLoader(const svc::Vfs& vfs, const CoreProfile& profile) noexcept
        : vfs_(&vfs),
          profile_(profile) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    [[nodiscard]] Ex<LoadPlan> plan_romset(std::span<const neogeo::RomsetFile> entries);

    void shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) override;

    [[nodiscard]] Ex<void> finish(infra::OptRef<ICoreWindow>) override {
        TASTY_SEAT_BODY(NeoGeoLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(NeoGeoLoader);
        return {};
    }

    [[nodiscard]] std::uint32_t skipped() const noexcept { return walked_.skipped; }

    [[nodiscard]] std::uint32_t config() const noexcept { return walked_.config; }

private:
    [[nodiscard]] neogeo::IRomShape* shaper_(neogeo::Shape kind) noexcept;

    const svc::Vfs* vfs_;
    const CoreProfile& profile_;

    std::array<FixedStr<kPathChars, StrFit::Reject>, kMaxTransferRows> paths_{};
    FixedStr<kPathChars, StrFit::Reject> save_{};
    neogeo::FixShape fix_shape_{};
    neogeo::SprShape spr_shape_{};
    neogeo::SprDblShape spr_dbl_shape_{};
    neogeo::SprDblSwapShape spr_dbl_swap_shape_{};
    neogeo::WalkTally walked_{};
};

}
