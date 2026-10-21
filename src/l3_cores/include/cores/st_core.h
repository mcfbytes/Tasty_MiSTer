// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/config_slots.h"
#include "cores/core_support.h"
#include "cores/mounted_path.h"
#include "cores/option_row_view.h"
#include "cores/option_rows.h"
#include "cores/sector_kick.h"
#include "cores/slot_mount.h"
#include "cores/st_config.h"
#include "cores/st_config_layout.h"
#include "cores/st_option_row.h"
#include "cores/st_options.h"
#include "cores/st_sysctrl.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "proto/disk_poll_decl.h"

namespace mister::cores {

class StCore final : public Core,
                     public ISectorKick,
                     public ISlotMount,
                     public IConfigSlots,
                     public IOptionRows {
    TASTY_SEAT_RESIDENT(RT);

public:
    StCore(const CoreProfile& p, const HostServices& h);

    static constexpr IoIndex kFloppySlotA{0};
    static constexpr IoIndex kFloppySlotB{1};
    static constexpr IoIndex kAcsiSlot0{2};
    static constexpr IoIndex kAcsiSlot1{3};
    static constexpr IoIndex kTosSlot{4};
    static constexpr IoIndex kCartSlot{5};

    static constexpr IoIndex kClearIndex{3};
    static constexpr proto::WideIoIndex kTosLargeIndex{0};
    static constexpr proto::WideIoIndex kTosSmallIndex{1};
    static constexpr proto::WideIoIndex kCartIndex{2};

    static constexpr std::size_t kChunkBytes = 4096;
    static constexpr std::uint32_t kClearBytes = 16u * 1024u;
    static constexpr std::size_t kCartBytes = 128u * 1024u;
    static constexpr std::size_t kCartBufferBytes = kCartBytes + 4u;
    static constexpr std::uint64_t kTosLargeBytes = 256u * 1024u;
    static constexpr std::uint64_t kTosSmallBytes = 192u * 1024u;

    static constexpr std::size_t kPathBytes = st_cfg::kPathBytes;
    static constexpr std::size_t kAcsiTargets = StConfig::kAcsiTargets;

    static constexpr proto::DiskPollDecl
        kAcsiPoll{.opcode = 0x0C, .beats = 16, .head_byte = 10, .head_mask = 0x01};
    static_assert(proto::well_formed(kAcsiPoll));

    void service_kick() noexcept override;

    [[nodiscard]] MountState mount_slot(proto::SlotIndex slot, std::string_view path) override;

    [[nodiscard]] std::uint8_t slot_count() const noexcept override {
        TASTY_SEAT_BODY(StCore);
        return st_cfg::kSlotCount;
    }

    [[nodiscard]] FixedStr<kConfigNameChars, StrFit::Clip> file_name(
        ConfigSlot slot
    ) const noexcept override;

    [[nodiscard]] ConfigFit fit() const noexcept override {
        TASTY_SEAT_BODY(StCore);
        return ConfigFit::Partial;
    }

    [[nodiscard]] std::span<const std::byte> snapshot() noexcept override;
    [[nodiscard]] Ex<void> restore(ConfigBlob blob) override;
    [[nodiscard]] FixedStr<kConfigPreviewChars, StrFit::Clip> preview(
        ConfigBlob blob
    ) const noexcept override;

    [[nodiscard]] std::span<const OptionRow> option_rows() const noexcept override {
        TASTY_SEAT_BODY(StCore);
        return profile().option_rows;
    }

    [[nodiscard]] OptionRowView option_view(std::size_t row) noexcept override;
    [[nodiscard]] Ex<void> set_option(std::size_t row, std::uint8_t choice) override;

    [[nodiscard]] Ex<void> set_system_ctrl(std::uint32_t ctrl);
    [[nodiscard]] Ex<void> set_ext_ctrl(std::uint32_t ext);

    [[nodiscard]] std::uint32_t system_ctrl() const noexcept { return cfg_.system_ctrl; }

    [[nodiscard]] std::uint32_t ext_ctrl() const noexcept { return cfg_.ext_ctrl; }

    [[nodiscard]] std::uint32_t control_pushes() const noexcept { return control_pushes_; }

    [[nodiscard]] std::uint32_t cold_resets() const noexcept { return cold_resets_; }

    [[nodiscard]] std::uint32_t warm_resets() const noexcept { return warm_resets_; }

    [[nodiscard]] std::uint32_t tos_refusals() const noexcept { return tos_refusals_; }

    [[nodiscard]] std::uint64_t bytes_sent() const noexcept { return bytes_sent_; }

    [[nodiscard]] bool acsi_bound(std::uint8_t target) const noexcept;

    [[nodiscard]] std::string_view tos_path() const noexcept { return cfg_.tos_img.view(); }

    [[nodiscard]] std::string_view cart_path() const noexcept { return cart_img_.view(); }

    [[nodiscard]] std::string_view acsi_path(std::uint8_t target) const noexcept;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    [[nodiscard]] Ex<void> on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) override;
    [[nodiscard]] Ex<void> on_apply_load_facts(std::span<const std::uint8_t> facts) override;

    [[nodiscard]] ISectorKick* on_sector_kick() noexcept override { return this; }

    [[nodiscard]] ISlotMount* on_slot_mount() noexcept override { return this; }

    [[nodiscard]] IConfigSlots* on_config_slots() noexcept override { return this; }

    [[nodiscard]] IOptionRows* on_options() noexcept override { return this; }

    void on_set_pending_file_ext(std::string_view ext) noexcept override;

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override { return bytes_sent_; }

    [[nodiscard]] Ex<void> push_control_();
    [[nodiscard]] Ex<void> update_sysctrl_(std::uint32_t ctrl);

    [[nodiscard]] Ex<void> clear_low_ram_();
    [[nodiscard]] Ex<void> tx_tos_(std::span<const std::uint8_t> bytes, std::string_view ext);

    [[nodiscard]] Ex<void> tx_cart_from_(std::span<const std::uint8_t> image);
    [[nodiscard]] Ex<void> tx_cart_(std::span<const std::uint8_t> rom);
    [[nodiscard]] Ex<void> insert_disk_(IoIndex slot, std::string_view path);

    [[nodiscard]] Ex<void> eject_hdd_(std::uint8_t target);
    [[nodiscard]] Ex<void> eject_all_();
    [[nodiscard]] std::uint32_t word_(WordId w) const noexcept;
    [[nodiscard]] Ex<void> push_word_(WordId w, std::uint32_t v);

    [[nodiscard]] StOptionRow::Facts facts_() const noexcept;
    [[nodiscard]] Ex<void> apply_write_(const StOptionRow::Write& w);

    StConfig cfg_{};

    FixedStr<kPathBytes, StrFit::Clip> cart_img_{};
    std::array<bool, kAcsiTargets> hdd_bound_{};
    std::array<bool, kAcsiTargets> bind_out_{};
    std::array<bool, 2> floppy_inserted_{};
    std::array<FixedStr<kPathBytes, StrFit::Clip>, 2> floppy_img_{};
    char view_text_[kOptionTextChars + 1]{};
    FixedStr<8, StrFit::Clip> ext_{};

    std::uint32_t control_pushes_ = 0;
    std::uint32_t cold_resets_ = 0;
    std::uint32_t warm_resets_ = 0;
    std::uint32_t tos_refusals_ = 0;
    std::uint64_t bytes_sent_ = 0;
    std::array<std::byte, st_cfg::kBytes> image_{};
};

}
