// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cores/boot_asset.h"
#include "cores/core_profile.h"
#include "cores/ladder_carry.h"
#include "cores/ladder_context.h"
#include "cores/ladder_host.h"
#include "cores/ladder_pc.h"
#include "cores/payload_pieces.h"
#include "cores/wire_word.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/types.h"
#include "svc/vfs.h"

namespace mister::cores {

class BootLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Report {
        bool same_game = false;
        bool bios_found = false;
        bool disc_mounted = false;
        bool save_mounted = false;
        bool save_detached = false;
        bool region_written = false;
        bool savestate_hold_expired = false;
        proto::RegionCode region{};
        std::uint32_t assets_loaded = 0;
        std::uint32_t rungs = 0;
        std::uint32_t bios_missing = 0;

        proto::FileSize bytes_downloaded{};
    };

    virtual ~BootLadder() = default;
    BootLadder(const BootLadder&) = delete;
    BootLadder& operator=(const BootLadder&) = delete;

    [[nodiscard]] Ex<bool> step();

    [[nodiscard]] LadderPc pc() const noexcept { return do_pc(); }

    [[nodiscard]] bool done() const noexcept { return pc().is_done(); }

    [[nodiscard]] const Report& report() const noexcept { return report_; }

    [[nodiscard]] LadderCarry carry() const noexcept { return do_carry(); }

private:
    [[nodiscard]] virtual LadderPc do_pc() const noexcept = 0;

    [[nodiscard]] virtual LadderCarry do_carry() const noexcept {
        return LadderCarry{.last_dir = image_dir_};
    }

    [[nodiscard]] virtual Ex<bool> on_step() = 0;

    [[nodiscard]] virtual bool steps_at_core_start() const noexcept { return false; }

protected:
    BootLadder(const CoreProfile& profile, const LadderContext& ctx);

    [[nodiscard]] bool core_start_armed() const noexcept { return core_start_; }

    [[nodiscard]] std::optional<std::string> resolve_row(const BootAsset& row) const;

    [[nodiscard]] Ex<AssetWait> load_row(const BootAsset& row);

    [[nodiscard]] Ex<AssetWait> step_pieces_();

    [[nodiscard]] bool group_gates(BootGroup group) const;

    [[nodiscard]] Ex<std::string> save_path_for(std::string_view image);

    [[nodiscard]] Ex<std::string> dir_save_path_for(std::string_view dir);

    [[nodiscard]] MountState mount_bounded(std::string_view path);

    [[nodiscard]] MountState mount_slot_bounded(proto::SlotIndex slot, std::string_view path);

    [[nodiscard]] bool mount_outstanding() const noexcept { return static_cast<bool>(mount_gen_); }

    [[nodiscard]] const MountStatus& mount_answer() const noexcept { return mount_answer_; }

    [[nodiscard]] bool prepare_save(std::string_view path);

    [[nodiscard]] proto::FileSize save_size() const noexcept { return save_size_; }

    [[nodiscard]] std::string_view save_name() const noexcept { return save_path_; }

    [[nodiscard]] Ex<void> resolve_save_();

    [[nodiscard]] Ex<void> resolve_dir_save_(std::string_view dir);

    [[nodiscard]] Ex<bool> prepare_save_once_();

    [[nodiscard]] Ex<bool> mount_save_rung_();

    [[nodiscard]] bool hold_reset_();

    [[nodiscard]] bool release_reset_();

    [[nodiscard]] bool pulse_reset_(std::chrono::microseconds pulse);

    [[nodiscard]] bool reset_held_() const noexcept { return bit0_asserted_; }

    [[nodiscard]] bool order_mount_save(std::string_view path, bool bracketed);
    [[nodiscard]] bool order_announce_disc(proto::FileSize size_bytes);
    [[nodiscard]] bool order_notify_mount(bool loaded);
    [[nodiscard]] bool order_status_bit0(bool asserted);
    [[nodiscard]] bool order_reset();
    [[nodiscard]] bool order_disc_payload(const proto::LinkOp::StageDiscPayload& facts = {});

    [[nodiscard]] bool order_empty_bracket(WideIoIndex dest);

    [[nodiscard]] bool order_set_index(WideIoIndex dest);
    [[nodiscard]] std::chrono::nanoseconds now_ns() const;

    [[nodiscard]] proto::ActKey next_gen() noexcept { return host_.next_generation(); }

    const CoreProfile& profile_;
    const svc::Vfs& vfs_;
    ILadderHost& host_;
    const os::IClock& clock_;

    [[nodiscard]] std::string_view image_path() const noexcept { return image_; }

    [[nodiscard]] std::string_view image_dir() const noexcept { return image_dir_; }

    FixedStr<1024, StrFit::Clip> load_path_{};
    std::optional<PayloadPieces> pieces_{};

    enum class Half : std::uint8_t {
        Fresh,
        FirstOrdered,
        Settled,
    };

    Report report_{};

private:
    std::string image_;
    std::string image_dir_;
    std::string save_path_;
    bool save_ready_ = false;
    std::chrono::nanoseconds reset_hold_ns_{};
    std::chrono::nanoseconds mount_due_ns_{};
    bool bit0_asserted_ = false;

    [[nodiscard]] Ex<std::string> save_root_();

    [[nodiscard]] bool finish_acts_();

    [[nodiscard]] Ex<bool> step_start_row_();

    std::span<const BootAsset> start_rows_{};
    std::size_t start_row_ = 0;
    bool core_start_ = false;
    bool index0_taken_ = false;
    bool cheats_ordered_ = false;
    proto::ActKey mount_gen_{};
    proto::ActKey save_gen_{};
    proto::FileSize save_size_{};
    bool save_exists_ = false;
    MountStatus mount_answer_{};
};

}
