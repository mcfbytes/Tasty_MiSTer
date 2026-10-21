// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/core_profile.h"
#include "cores/loader.h"
#include "cores/romsets.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/status_word.h"
#include "svc/vfs.h"

namespace mister::cores {

namespace neogeo {

inline constexpr std::uint8_t kPromIndex = 4;
inline constexpr std::uint8_t kCromNotifyIndex = 15;
inline constexpr std::uint8_t kCromFirstIndex = 64;
inline constexpr std::uint8_t kLoRomIndex = 1;
inline constexpr std::uint32_t kLoRomBytes = 0x10000;
inline constexpr std::uint32_t kLoadBufBytes = 0x10'0000;

inline constexpr std::uint32_t kCdEnBit = 1u << 31;
inline constexpr std::uint8_t kFillIndex = 18;
inline constexpr std::uint32_t kFillBytes = 128u * 1024u;
inline constexpr std::uint8_t kFillPattern = 0xAA;
inline constexpr std::uint32_t kCdBiosBytes = 0x8'0000;

inline constexpr proto::StatusBit kMvsBit{1};
inline constexpr proto::StatusBit kCdzBit{2};
inline constexpr proto::StatusBit kOriginalBiosBit{22};

enum class Shape : std::uint8_t { None, Crom, Fix, SprDbl, SprDblSwap };

struct RomEntry {
    std::string_view path{};
    std::uint8_t index = 0;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    FileClass cls = FileClass::Raw;
    std::uint32_t expand = 0;
    bool swap = false;
};

[[nodiscard]] constexpr bool notify_memcp(std::uint8_t index, bool force = false) noexcept {
    return force || !(index == 9 || (index >= 16 && index < 64));
}

[[nodiscard]] constexpr std::uint32_t notify_size(std::uint8_t index, std::uint32_t size) noexcept {
    constexpr std::uint32_t kAlign = 0x10'0000u - 1u;
    return (index == 4 || index == 6) ? ((size + kAlign) & ~kAlign) : size;
}

[[nodiscard]] constexpr PostNotify make_notify(
    std::uint8_t index,
    std::uint32_t size,
    bool force = false
) noexcept {
    const std::uint32_t sz = notify_size(index, size);
    return PostNotify{
        .armed = true,
        .words = {
            index,
            static_cast<std::uint16_t>(sz & 0xFFFFu),
            static_cast<std::uint16_t>(sz >> 16),
            static_cast<std::uint16_t>(notify_memcp(index, force) ? 1u : 0u),
            0u
        }
    };
}

[[nodiscard]] constexpr PostNotify make_conf_notify(std::uint32_t conf) noexcept {
    return PostNotify{
        .armed = true,
        .words = {
            0x8000u,
            static_cast<std::uint16_t>(conf & 0xFFFFu),
            static_cast<std::uint16_t>(conf >> 16),
            0u,
            0u
        }
    };
}

[[nodiscard]] constexpr std::uint8_t rom_fill(std::uint8_t index) noexcept {
    return (index >= 16 && index < 64) ? 8u : 0u;
}

[[nodiscard]] constexpr std::uint32_t crom_offset(std::uint8_t index) noexcept {
    return static_cast<std::uint32_t>((index - kCromFirstIndex) >> 1) * 0x10'0000u;
}

[[nodiscard]] constexpr std::uint8_t crom_lane(std::uint8_t index) noexcept {
    return static_cast<std::uint8_t>((index ^ 1u) & 1u);
}

struct RomPlace {
    std::uint8_t window = 0;
    std::uint32_t offset = 0;
};

[[nodiscard]] constexpr RomPlace rom_place(std::uint8_t index, std::uint32_t concat) noexcept {
    if (concat != 0)
        return {0, concat};
    if (index >= 16 && index < 64)
        return {1, (index - 16u) * 0x8'0000u};
    if (index == 9)
        return {1, 0x200'0000u};
    return {0, 0};
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
    static constexpr std::uint8_t kMemWindow = 0;
    static constexpr std::uint8_t kRomWindow = 1;
    static constexpr std::size_t kPathChars = 1024;

    explicit NeoGeoLoader(const svc::Vfs& vfs, const CoreProfile* profile = nullptr) noexcept
        : vfs_(&vfs),
          profile_(profile) {}

    [[nodiscard]] Ex<LoadPlan> plan(const LoadAsk& ask, const proto::StatusWord& status) override;

    [[nodiscard]] Ex<LoadPlan> plan_romset(std::span<const neogeo::RomEntry> entries);

    void shape(const TransferRow& row, std::span<std::uint8_t> piece, std::uint64_t off) override;

    [[nodiscard]] Ex<void> finish(ICoreWindow*) override {
        TASTY_SEAT_BODY(NeoGeoLoader);
        return {};
    }

    [[nodiscard]] std::span<const std::uint8_t> facts() const noexcept override {
        TASTY_SEAT_BODY(NeoGeoLoader);
        return {};
    }

    [[nodiscard]] std::uint32_t skipped() const noexcept { return skipped_; }

    [[nodiscard]] std::uint32_t config() const noexcept { return config_; }

private:
    struct Span {
        std::uint32_t size = 0;
        std::uint64_t rest = 0;
    };

    [[nodiscard]] std::optional<Span> entry_span_(
        std::string_view path,
        std::uint32_t offset,
        std::uint32_t size
    ) const;
    [[nodiscard]] bool exists_(std::string_view path) const;
    [[nodiscard]] std::string home_(std::string_view dir) const;
    [[nodiscard]] Ex<void> frame_save_(LoadPlan& p, std::string_view pick);

    [[nodiscard]] Ex<TransferRow*> add_row_(LoadPlan& p, std::string_view path, RowDest dest);
    [[nodiscard]] Ex<std::uint32_t> add_entry_(LoadPlan& p, const neogeo::RomEntry& e);
    [[nodiscard]] Ex<std::uint32_t> add_crom_(LoadPlan& p, const neogeo::RomEntry& e);
    [[nodiscard]] Ex<std::uint32_t> add_rom_(
        LoadPlan& p,
        std::string_view path,
        const neogeo::RomEntry& e,
        std::uint32_t concat
    );
    [[nodiscard]] Ex<void> flush_crom_(LoadPlan& p);
    [[nodiscard]] Ex<void> add_notify_(LoadPlan& p, const PostNotify& n);

    [[nodiscard]] Ex<bool> add_lo_rom_(LoadPlan& p, std::string_view home);
    [[nodiscard]] Ex<void> add_fill_(LoadPlan& p);
    void reset_walk_() noexcept;

    [[nodiscard]] Ex<LoadPlan> plan_cart_(std::string_view pick, const proto::StatusWord& status);
    [[nodiscard]] Ex<LoadPlan> plan_cd_(std::string_view pick, const proto::StatusWord& status);
    [[nodiscard]] Ex<std::optional<std::uint32_t>> add_neo_(LoadPlan& p, std::string_view pick);
    [[nodiscard]] Ex<std::optional<std::uint32_t>> add_xml_(
        LoadPlan& p,
        std::string_view pick,
        std::string_view home
    );

    const svc::Vfs* vfs_;
    const CoreProfile* profile_;
    std::array<FixedStr<kPathChars, StrFit::Reject>, kMaxTransferRows> paths_{};
    FixedStr<kPathChars, StrFit::Reject> save_{};
    std::vector<std::uint16_t> in_;
    std::vector<std::uint16_t> out_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t> fix_out_;
    std::vector<std::uint32_t> dwords_;
    std::uint32_t skipped_ = 0;
    std::uint32_t crom_sz_ = 0;
    std::size_t last_crom_ = 0;
    std::uint32_t config_ = 0;
};

}
