// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "cores/boot_asset.h"
#include "cores/core_profile.h"
#include "cores/ladder_context.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "svc/vfs.h"

namespace mister::cores {

class BootLadder {
    TASTY_SEAT_EXEMPT(main);

public:
    struct Report {
        bool same_game = false;
        bool bios_found = false;
        bool disc_mounted = false;
        bool save_mounted = false;
        bool save_detached = false;
        bool region_written = false;
        std::uint8_t region = 0;
        std::uint32_t assets_loaded = 0;
        std::uint32_t rungs = 0;

        std::uint64_t bytes_downloaded = 0;
    };

    static constexpr std::uint8_t kDone = 0xFF;

    static constexpr std::uint16_t kGenericChunk = 4096;

    virtual ~BootLadder() = default;
    BootLadder(const BootLadder&) = delete;
    BootLadder& operator=(const BootLadder&) = delete;

    [[nodiscard]] Ex<bool> step();

    [[nodiscard]] virtual std::uint8_t pc() const noexcept = 0;

    [[nodiscard]] bool done() const noexcept { return pc() == kDone; }
    [[nodiscard]] const Report& report() const noexcept { return report_; }

    [[nodiscard]] virtual std::string_view next_last_dir() const noexcept { return image_dir_; }
    [[nodiscard]] virtual bool next_noreset() const noexcept { return false; }

protected:
    BootLadder(const CoreProfile& profile, const LadderContext& ctx);

    [[nodiscard]] virtual Ex<bool> on_step() = 0;

    [[nodiscard]] virtual bool steps_at_core_start() const noexcept { return false; }

    static std::string_view dir_of(std::string_view path);
    static std::string_view base_of(std::string_view path);

    static std::string_view ext_of(std::string_view path);
    static std::string strip_disc_word(std::string path, std::span<const std::string_view> words);

    [[nodiscard]] std::optional<std::string> resolve_row(const BootAsset& row) const;

    [[nodiscard]] Ex<AssetWait> load_row(const BootAsset& row);

    [[nodiscard]] Ex<AssetWait> step_pieces_();

    [[nodiscard]] bool group_gates(std::uint8_t group) const;

    [[nodiscard]] Ex<std::string> save_path_for(std::string_view image);

    [[nodiscard]] MountState mount_bounded(std::string_view path);

    [[nodiscard]] const MountStatus& mount_answer() const noexcept { return mount_answer_; }

    [[nodiscard]] bool prepare_save(std::string_view path);
    [[nodiscard]] std::uint64_t save_size() const noexcept { return save_size_; }

    [[nodiscard]] bool order_mount_save(std::string_view path, bool bracketed);
    [[nodiscard]] bool order_announce_disc(std::uint64_t size_bytes);
    [[nodiscard]] bool order_notify_mount(bool loaded);
    [[nodiscard]] bool order_status_bit0(bool asserted);
    [[nodiscard]] bool order_reset();
    [[nodiscard]] bool order_disc_payload(const proto::LinkOp::StageDiscPayload& facts = {});

    [[nodiscard]] bool order_empty_bracket(WideIoIndex dest);

    [[nodiscard]] bool order_set_index(WideIoIndex dest);
    [[nodiscard]] std::int64_t now_ns() const;
    [[nodiscard]] std::uint32_t next_gen() noexcept { return host_.next_generation(); }

    const CoreProfile& profile_;
    const svc::Vfs& vfs_;
    ILadderHost& host_;
    const os::IClock& clock_;

    std::string image_;
    std::string image_dir_;
    std::string save_path_;
    FixedStr<1024, StrFit::Clip> load_path_{};
    std::optional<PayloadPieces> pieces_{};

    std::uint8_t row_i_ = 0;

    std::uint8_t sub_ = 0;
    bool marker_seen_ = false;
    bool save_ready_ = false;
    std::int64_t pulse_release_ns_ = 0;
    std::int64_t mount_due_ns_ = 0;
    bool bit0_asserted_ = false;

    Report report_{};

private:
    [[nodiscard]] bool finish_acts_();

    [[nodiscard]] Ex<bool> step_start_row_();

    std::span<const BootAsset> start_rows_{};
    std::size_t start_row_ = 0;
    bool core_start_ = false;
    bool index0_taken_ = false;
    bool cheats_ordered_ = false;
    std::uint32_t mount_gen_ = 0;
    std::uint32_t save_gen_ = 0;
    std::uint64_t save_size_ = 0;
    MountStatus mount_answer_{};
};

}  // namespace mister::cores
