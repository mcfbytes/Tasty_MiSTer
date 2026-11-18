// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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
#include "cores/st_ctrl.h"
#include "cores/st_option_row.h"
#include "cores/st_options.h"
#include "cores/st_sysctrl.h"
#include "cores/stream_load.h"
#include "cores/stream_load_host.h"
#include "infra/fixed_str.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "proto/disk_poll_decl.h"

namespace mister::cores {

struct OptionDetail {

    std::uint32_t v = 0;

    [[nodiscard]] static constexpr OptionDetail of(
        proto::FieldValue pick,
        proto::OptionRowIndex index
    ) noexcept {

        return OptionDetail{(pick.v << std::numeric_limits<std::uint8_t>::digits) | index.v};
    }

    [[nodiscard]] constexpr Error refuse(infra::ErrorSite at) const noexcept {
        return Error{Errc::slot_range, at, v};
    }
};

class StCore final : public Core,
                     public ISectorKick,
                     public ISlotMount,
                     public IConfigSlots,
                     public IOptionRows,
                     public IStreamLoad {
    TASTY_SEAT_RESIDENT(RT);

public:
    StCore(const CoreProfile& p, const HostServices& h);

    static constexpr IoIndex kClearIndex{3};

    static constexpr std::uint32_t kClearBytes = 16u * 1024u;

    static constexpr proto::FioWordCount kClearPieceWords{1024};
    static_assert(
        kClearBytes % kClearPieceWords.bytes_at(hal::Width::Word) == 0,
        "the clear is a whole number of pieces"
    );

    static constexpr std::size_t kPathBytes = st_cfg::kPathBytes;
    static constexpr std::size_t kAcsiTargets = StConfig::kAcsiTargets;

    static constexpr proto::DiskPollDecl
        kAcsiPoll{.opcode = 0x0C, .beats = 16, .head_byte = 10, .head_mask = 0x01};
    static_assert(proto::well_formed(kAcsiPoll));

    void service_kick() noexcept override;

    [[nodiscard]] MountState mount_slot(proto::SlotIndex slot, std::string_view path) override;

    [[nodiscard]] Ex<void> apply(ConfigBlob value) override;
    [[nodiscard]] std::span<const std::byte> value(
        std::span<std::byte> dst
    ) const noexcept override;

    [[nodiscard]] std::span<const OptionRow> option_rows() const noexcept override {
        TASTY_SEAT_BODY(StCore);
        return profile().option_rows;
    }

    [[nodiscard]] OptionRowView option_view(proto::OptionRowIndex row) noexcept override;
    [[nodiscard]] Ex<void> set_option(proto::OptionRowIndex row, proto::FieldValue choice) override;

    [[nodiscard]] Ex<proto::SessionParams> stream_opening(const StreamAsk& ask) override;
    void stream_closed(const StreamLoadEnd& end) noexcept override;

    [[nodiscard]] Ex<void> set_system_ctrl(std::uint32_t ctrl);
    [[nodiscard]] Ex<void> set_ext_ctrl(std::uint32_t ext);

    [[nodiscard]] st_ctrl::CtrlWord system_ctrl() const noexcept { return cfg_.system_ctrl; }

    [[nodiscard]] std::uint32_t ext_ctrl() const noexcept { return cfg_.ext_ctrl; }

    [[nodiscard]] std::uint32_t control_pushes() const noexcept { return control_pushes_; }

    [[nodiscard]] std::uint32_t cold_resets() const noexcept { return cold_resets_; }

    [[nodiscard]] std::uint32_t warm_resets() const noexcept { return warm_resets_; }

    [[nodiscard]] std::uint32_t no_tos_refusals() const noexcept { return no_tos_refusals_; }

    [[nodiscard]] bool acsi_bound(AcsiTarget target) const noexcept;

    [[nodiscard]] std::string_view tos_path() const noexcept { return cfg_.tos_img.view(); }

    [[nodiscard]] std::string_view cart_path() const noexcept { return cart_img_.view(); }

    [[nodiscard]] std::string_view acsi_path(AcsiTarget target) const noexcept;

private:
    [[nodiscard]] Ex<void> do_adopt(const CoreAdopt& a) override;
    [[nodiscard]] Ex<void> on_mount(IoIndex slot, const MountedPath& p) override;
    [[nodiscard]] Ex<void> on_apply_load_facts(
        std::span<const std::uint8_t> facts,
        std::string_view
    ) override;

    [[nodiscard]] infra::OptRef<ISectorKick> on_sector_kick() noexcept override {
        return infra::OptRef<ISectorKick>{*this};
    }

    [[nodiscard]] infra::OptRef<ISlotMount> on_slot_mount() noexcept override {
        return infra::OptRef<ISlotMount>{*this};
    }

    [[nodiscard]] infra::OptRef<IConfigSlots> on_config_slots() noexcept override {
        return infra::OptRef<IConfigSlots>{*this};
    }

    [[nodiscard]] infra::OptRef<IOptionRows> on_options() noexcept override {
        return infra::OptRef<IOptionRows>{*this};
    }

    [[nodiscard]] infra::OptRef<IStreamLoad> on_stream_load() noexcept override {
        return infra::OptRef<IStreamLoad>{*this};
    }

    [[nodiscard]] std::uint64_t on_last_tx_bytes() const noexcept override {
        return stream_.bytes();
    }

    [[nodiscard]] Ex<void> push_control_();
    [[nodiscard]] Ex<void> update_sysctrl_(st_ctrl::CtrlWord ctrl);

    void store_sysctrl_(st_ctrl::CtrlWord ctrl) noexcept;

    [[nodiscard]] Ex<void> insert_disk_(st_ctrl::StSlot slot, std::string_view path);

    [[nodiscard]] Ex<void> eject_hdd_(AcsiTarget target);
    [[nodiscard]] Ex<void> eject_all_();
    [[nodiscard]] st_ctrl::CtrlWord word_(WordId w) const noexcept;
    [[nodiscard]] Ex<void> push_word_(WordId w, st_ctrl::CtrlWord v);

    [[nodiscard]] StOptionRow::Facts facts_() const noexcept;
    [[nodiscard]] Ex<void> apply_write_(const StOptionRow::Write& w);

    StConfig cfg_{};

    FixedStr<kPathBytes, StrFit::Clip> cart_img_{};

    struct AcsiBind {
        bool bound = false;
        bool out = false;
    };

    infra::IndexedArray<AcsiBind, kAcsiTargets, AcsiTarget> acsi_{};
    std::array<FixedStr<kPathBytes, StrFit::Clip>, 2> floppy_img_{};
    char view_text_[kOptionTextChars + 1]{};

    StreamLoadHost stream_;

    std::uint32_t control_pushes_ = 0;
    std::uint32_t cold_resets_ = 0;
    std::uint32_t warm_resets_ = 0;
    std::uint32_t no_tos_refusals_ = 0;
};

}
