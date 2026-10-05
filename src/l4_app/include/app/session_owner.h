// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <deque>
#include <vector>

#include "app/board_ops.h"
#include "app/bitstream_programmer.h"
#include "app/conf_str_cell.h"
#include "app/ladder_cell.h"
#include "app/mount_status_cell.h"
#include "proto/save_ask.h"
#include "app/save_extent_cell.h"
#include "proto/status_cell.h"
#include "app/fabric_state.h"
#include "app/fallback_counts.h"
#include "app/quiesce.h"
#include "app/addon_send.h"
#include "app/companion_host.h"
#include "app/companion_walk.h"
#include "app/content_request.h"
#include "app/file_tx_pieces.h"
#include "app/file_tx_level.h"
#include "app/load_ladder.h"
#include "app/load_walk.h"
#include "app/load_window_counts.h"
#include "app/load_window_map.h"
#include "app/mra_facts.h"
#include "app/path_text.h"
#include "app/pending_load.h"
#include "app/remembered_path.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "cores/companion_load.h"
#include "cores/loader_memo.h"
#include "cores/boot_ladder.h"
#include "cores/ladder_host.h"
#include "infra/pause_latch.h"
#include "app/launcher_demand.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "os/clock.h"
#include "svc/dir_entry.h"
#include "os/deadline.h"
#include "os/monotonic_clock.h"
#include "hal/thread_map.h"
#include "app/ini_parse.h"
#include "app/link_tx_channel.h"
#include "app/link_rx_channel.h"
#include "infra/spsc_ring.h"
#include "hal/phys_region.h"
#include "hal/pin_levels.h"
#include "proto/conf_str.h"
#include "proto/link_event.h"

namespace mister::hal {
class IBootHandoff;
}

namespace mister::cores {
struct CoreFactory;
}

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class EventQueue;

class SessionOwner : private cores::ILadderHost {
    TASTY_SEAT_EXEMPT(main);

public:
    struct Link {
        LinkTxChannel& inbox;
        LinkRxChannel& rx;
        ConfigCell& config_cell;
        EventQueue& owner_events;
        ConfStrCell& conf_str_cell;

        xthread::WakeFlag* main_wake = nullptr;
    };

    SessionOwner(QuiesceChannel& park, const Link& link) noexcept
        : park_(&park), inbox_(link.inbox), rx_(link.rx), config_cell_(link.config_cell),
          owner_events_(link.owner_events), conf_str_cell_(link.conf_str_cell),
          ui_requests_(link.main_wake != nullptr ? UiRequestRing{*link.main_wake}
                                                 : UiRequestRing{}) {
        (void)front_end_image_.assign(kFrontEndImage);
    }

    SessionOwner(const SessionOwner&) = delete;
    SessionOwner& operator=(const SessionOwner&) = delete;

    void set_board_ops(const BoardOps& ops) noexcept { ops_ = ops; }

    void set_identity_poll_budget(std::uint32_t n) noexcept { identity_poll_budget_ = n; }

    static constexpr std::uint32_t kStartBoundMs = 5000;
    void set_start_bound_ms(std::uint32_t ms) noexcept { start_bound_ms_ = ms; }

    bool set_front_end_image(std::string_view rel) noexcept { return front_end_image_.assign(rel); }

    void set_readiness_poll_budget(std::uint32_t n) noexcept { readiness_poll_budget_ = n; }

    void set_boot_handoff(hal::IBootHandoff* page) noexcept { handoff_ = page; }

    void set_launcher_demand(LauncherDemandCell* cell, xthread::WakeFlag* wake) noexcept {
        launcher_cell_ = cell;
        launcher_wake_ = wake;
    }

    [[nodiscard]] const LauncherProfile* launcher() const noexcept {
        return launcher_demand_.profile;
    }

    void set_programmer(BitstreamProgrammer* p) noexcept { programmer_ = p; }
    void set_storage(const svc::Vfs* vfs) noexcept { vfs_ = vfs; }

    void set_aperture(hal::PhysRegion region) noexcept { aperture_ = region; }

    void set_fabric_cell(FabricCell* cell) {
        cell_ = cell;
        publish_fabric_();
    }

    void set_status_cell(const proto::StatusCell* cell) noexcept { status_cell_ = cell; }

    void set_mount_status_cell(const MountStatusCell* cell) noexcept { mount_status_cell_ = cell; }
    void set_save_extent_cell(const SaveExtentCell* cell) noexcept { save_extent_cell_ = cell; }

    [[nodiscard]] LadderStateCell& ladder_cell() noexcept { return ladder_cell_; }
    [[nodiscard]] const LadderStateCell& ladder_cell() const noexcept { return ladder_cell_; }

    void set_clock(const os::IClock& c) noexcept { clock_ = &c; }

    void grant_pause(hal::Seat s, xthread::PauseLatch& latch) noexcept {
        pauses_[static_cast<std::size_t>(s)] = &latch;
    }

    [[nodiscard]] bool program_owed() const noexcept;

    [[nodiscard]] int wake_hint_ms(int floor_ms) const noexcept;

    [[nodiscard]] std::uint32_t pause_expiries() const noexcept { return pause_expiries_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Unbound>& pause_expiries_cell()
        const noexcept {
        return pause_expiries_cell_;
    }

    [[nodiscard]] bool last_order_owed() const noexcept { return last_order_.has_value(); }

    [[nodiscard]] std::uint32_t orders_superseded() const noexcept { return orders_superseded_; }

    [[nodiscard]] bool load_held() const noexcept { return held_load_.has_value(); }
    [[nodiscard]] std::uint32_t loads_held() const noexcept { return loads_held_; }
    [[nodiscard]] std::uint32_t loads_cancelled() const noexcept { return loads_cancelled_; }

    [[nodiscard]] bool recovery_owed() const noexcept { return recover_owed_; }

    [[nodiscard]] bool front_end_owed() const noexcept { return front_end_owed_; }
    [[nodiscard]] const FallbackCounts& fallback_counts() const noexcept { return fallbacks_; }

    [[nodiscard]] const xthread::Telemetry<FallbackCounts, SeatTag::Unbound>& fallback_cell()
        const noexcept {
        return fallback_cell_;
    }

    [[nodiscard]] std::uint32_t switch_belt_refusals() const noexcept {
        return switch_belt_refusals_;
    }

    [[nodiscard]] std::uint32_t reboot_refusals() const noexcept { return reboot_refusals_; }

    void set_window_map(ILoadWindowMap* m) noexcept { window_map_ = m; }
    void set_file_tx_level_cell(const FileTxLevelCell* c) noexcept { ftx_level_cell_ = c; }

    [[nodiscard]] bool load_live() const noexcept { return load_.has_value(); }
    [[nodiscard]] const LoadLadder* load() const noexcept { return load_ ? &*load_ : nullptr; }

    [[nodiscard]] const LoadWalk* walk() const noexcept { return walk_ ? &*walk_ : nullptr; }

    void set_companion_binds(CompanionHost::Binds* b) noexcept { companion_binds_ = b; }

    [[nodiscard]] const CompanionWalk* companion_walk() const noexcept {
        return companion_walk_ ? &*companion_walk_ : nullptr;
    }
    [[nodiscard]] std::uint32_t companion_walks() const noexcept { return companion_walks_; }
    [[nodiscard]] std::uint32_t companion_skips() const noexcept { return companion_skips_; }

    [[nodiscard]] std::uint32_t companion_attach_refusals() const noexcept {
        return companion_attach_refusals_;
    }
    [[nodiscard]] const LoadWindowCounts& window_counts() const noexcept { return window_counts_; }
    [[nodiscard]] const xthread::Telemetry<LoadWindowCounts, SeatTag::Unbound>& window_cell()
        const noexcept {
        return window_cell_;
    }

    [[nodiscard]] bool pieces_live() const noexcept { return pieces_.has_value(); }

    [[nodiscard]] bool rung_pending() const noexcept;

    [[nodiscard]] std::uint32_t loads_refused_busy() const noexcept { return loads_refused_busy_; }

    [[nodiscard]] std::uint32_t savestate_refusals() const noexcept { return savestate_refusals_; }

    void set_pin_levels(const hal::PinLevelCell* levels) noexcept { levels_ = levels; }

    void set_boot_cookie(std::uint16_t c) {
        cookie_ = c;
        publish_fabric_();
    }

    unsigned tick();

    void service_boot(const UiRequest::LoadCore& req);

    void expect_boot_negotiate() noexcept;

    void on_seats_live() noexcept { before_seats_ = false; }

    void on(const UiRequest::LoadCore& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveDips& req, const UiRequest::Head& head);
    void on(const UiRequest::SaveCoreConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadCoreConfig& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadFile& req, const UiRequest::Head& head);
    void on(const UiRequest::MountImage& req, const UiRequest::Head& head);
    void on(const UiRequest::UnmountImage& req, const UiRequest::Head& head);
    void on(const UiRequest::ResetCore& req, const UiRequest::Head& head);
    void on(const UiRequest::Reboot& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadFileByDigit& req, const UiRequest::Head& head);
    void on(const UiRequest::LoadRamImage& req, const UiRequest::Head& head);
    void misrouted(const UiRequest& m) noexcept;

    void on(const proto::LinkEvent::ReadyEdge& e) noexcept;
    void on(const proto::LinkEvent::BlockRequest& e) noexcept;
    void on(const proto::LinkEvent::IdentityMatched& e) noexcept;
    void on(const proto::LinkEvent::IdentityMismatch& e) noexcept;
    void on(const proto::LinkEvent::ConfStr& e);
    void on(const proto::LinkEvent::SaveBytes& e);
    void on(const proto::LinkEvent::CoreMade& e) noexcept;
    void on(const proto::LinkEvent::StartRefused& e) noexcept;
    void on(const proto::LinkEvent::RebootQuiesced& e);
    void misrouted(const proto::LinkEvent& m) noexcept;

    [[nodiscard]] std::uint32_t recoveries() const noexcept { return recoveries_; }
    [[nodiscard]] std::uint32_t recover_polls() const noexcept { return recover_polls_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Unbound>& recover_polls_cell()
        const noexcept {
        return recover_polls_cell_;
    }

    [[nodiscard]] std::uint32_t reboots_settled() const noexcept { return reboots_settled_; }

    [[nodiscard]] bool reboot_owed() const noexcept { return reboot_owed_.has_value(); }

    [[nodiscard]] std::uint16_t reboot_seq() const noexcept { return reboot_seq_; }
    [[nodiscard]] std::uint32_t reboot_drain_expiries() const noexcept {
        return reboot_drain_expiries_;
    }
    [[nodiscard]] std::uint32_t order_refusals() const noexcept { return order_refusals_; }

    [[nodiscard]] std::uint32_t switches_asked() const noexcept { return switches_asked_; }
    [[nodiscard]] std::uint32_t ask_refusals() const noexcept { return ask_refusals_; }

    [[nodiscard]] std::uint32_t load_core_refusals() const noexcept { return load_core_refusals_; }
    [[nodiscard]] bool switch_standing() const noexcept { return switch_standing_; }
    [[nodiscard]] std::uint32_t stale_acks() const noexcept { return stale_acks_; }
    [[nodiscard]] std::uint32_t stale_events() const noexcept { return stale_events_; }
    [[nodiscard]] std::uint32_t core_make_refusals() const noexcept { return core_make_refusals_; }

    [[nodiscard]] std::uint32_t ui_request_unhandled() const noexcept {
        return ui_request_unhandled_;
    }
    [[nodiscard]] std::uint32_t ui_request_misrouted() const noexcept {
        return ui_request_misrouted_;
    }

    [[nodiscard]] std::uint32_t link_event_unhandled() const noexcept {
        return link_event_unhandled_;
    }
    [[nodiscard]] std::uint32_t link_event_misrouted() const noexcept {
        return link_event_misrouted_;
    }

    [[nodiscard]] std::uint32_t fabric_wedges() const noexcept { return fabric_wedges_; }

    [[nodiscard]] const std::optional<proto::ConfStr>& conf_str() const noexcept {
        return conf_str_;
    }

    [[nodiscard]] CoreScope conf_scope() const noexcept { return CoreScope{conf_str_gen_}; }

    [[nodiscard]] std::uint32_t mgl_row0() const noexcept { return mgl_row0_; }
    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Unbound>& mgl_row0_cell()
        const noexcept {
        return mgl_row0_cell_;
    }

    [[nodiscard]] static std::optional<PathText> bare_zip_member(
        std::string_view path, std::string_view exts, std::span<const svc::DirEntry> listing);

    [[nodiscard]] std::uint32_t gen() const noexcept { return gen_; }

    [[nodiscard]] const UiRequest::LoadCore& pending() const noexcept { return pending_; }
    [[nodiscard]] CorrelationTag pending_tag() const noexcept { return pending_tag_; }

    [[nodiscard]] std::uint32_t programs_run() const noexcept { return programs_run_; }
    [[nodiscard]] std::uint32_t programs_failed() const noexcept { return programs_failed_; }

    [[nodiscard]] std::uint32_t files_loaded() const noexcept { return files_loaded_; }
    [[nodiscard]] std::uint32_t start_assets_ordered() const noexcept {
        return start_assets_ordered_;
    }

    [[nodiscard]] std::uint32_t remembered_ordered() const noexcept { return remembered_ordered_; }
    [[nodiscard]] const RememberedStem& remembered_stem() const noexcept {
        return remembered_stem_;
    }

    [[nodiscard]] std::uint32_t remembered_mounts_armed() const noexcept {
        return remembered_mounts_armed_;
    }
    [[nodiscard]] std::uint32_t remembered_mounts_skipped() const noexcept {
        return remembered_mounts_skipped_;
    }
    [[nodiscard]] std::size_t start_mounts_owed() const noexcept { return start_mounts_.size(); }
    [[nodiscard]] std::uint32_t remembered_missed() const noexcept { return remembered_missed_; }
    [[nodiscard]] std::uint32_t files_failed() const noexcept { return files_failed_; }

    [[nodiscard]] std::uint32_t addons_sent() const noexcept { return addons_sent_; }
    [[nodiscard]] std::uint32_t addons_missing() const noexcept { return addons_missing_; }

    [[nodiscard]] std::uint32_t ram_images_sent() const noexcept { return ram_images_sent_; }
    [[nodiscard]] std::uint32_t ram_images_declined() const noexcept {
        return ram_images_declined_;
    }

    [[nodiscard]] const cores::LoaderMemo& loader_memo() const noexcept { return loader_memo_; }
    [[nodiscard]] bool walk_live() const noexcept { return walk_.has_value(); }

    [[nodiscard]] std::uint32_t images_sized() const noexcept { return images_sized_; }

    [[nodiscard]] std::uint32_t config_saves() const noexcept { return config_saves_; }

    [[nodiscard]] std::uint32_t save_write_failures() const noexcept {
        return save_write_failures_;
    }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Unbound>&
    save_write_failures_cell() const noexcept {
        return save_write_failures_cell_;
    }

    [[nodiscard]] LinkTxChannel& inbox() noexcept { return inbox_; }
    [[nodiscard]] const LinkTxChannel& inbox() const noexcept { return inbox_; }
    [[nodiscard]] proto::LinkTxRing& tx() noexcept { return inbox_.ring(); }
    [[nodiscard]] const proto::LinkTxRing& tx() const noexcept { return inbox_.ring(); }
    [[nodiscard]] std::uint32_t ops_posted() const noexcept { return ops_posted_; }
    [[nodiscard]] std::uint32_t op_drops() const noexcept { return op_drops_; }

    [[nodiscard]] LinkRxChannel& rx() noexcept { return rx_; }
    [[nodiscard]] const LinkRxChannel& rx() const noexcept { return rx_; }
    [[nodiscard]] ConfigCell& config_cell() noexcept { return config_cell_; }

    [[nodiscard]] bool boot_config_parsed() const noexcept { return !boot_config_pending_; }

    [[nodiscard]] const std::string& boot_handoff() const noexcept { return boot_handoff_; }

    void arm_replay_ini(bool strict) noexcept;

    [[nodiscard]] const xthread::Telemetry<std::uint8_t, SeatTag::Unbound>& direct_video_ini_cell()
        const noexcept {
        return direct_video_ini_cell_;
    }
    [[nodiscard]] std::uint32_t identities_ok() const noexcept { return identities_ok_; }
    [[nodiscard]] std::uint32_t identities_fail() const noexcept { return identities_fail_; }

    [[nodiscard]] std::uint32_t conf_str_parse_refusals() const noexcept {
        return conf_str_parse_refusals_;
    }

    [[nodiscard]] std::uint32_t identity_intern_refusals() const noexcept {
        return identity_intern_refusals_;
    }

    [[nodiscard]] std::uint32_t doorbell_intern_refusals() const noexcept {
        return doorbell_intern_refusals_;
    }

    [[nodiscard]] bool park_acks_empty() const noexcept { return park_->ack_empty(); }

    [[nodiscard]] UiRequestRing& ui_requests() noexcept { return ui_requests_; }

    [[nodiscard]] std::uint32_t mounts_armed() const noexcept { return mounts_armed_; }
    [[nodiscard]] std::uint32_t mounts_held() const noexcept { return mounts_held_; }
    [[nodiscard]] std::uint32_t mounts_refused() const noexcept { return mounts_refused_; }

    [[nodiscard]] std::uint32_t ladders_run() const noexcept { return ladders_run_; }
    [[nodiscard]] std::uint32_t ladder_failures() const noexcept { return ladder_failures_; }

    [[nodiscard]] bool ladder_live() const noexcept { return ladder_ != nullptr; }

    [[nodiscard]] std::uint32_t resets_ordered() const noexcept { return resets_ordered_; }
    [[nodiscard]] bool reset_owed() const noexcept { return reset_owed_.has_value(); }

    [[nodiscard]] bool save_mount_live() const noexcept { return save_mount_.has_value(); }
    [[nodiscard]] std::uint32_t save_mounts_done() const noexcept { return save_mounts_done_; }

    [[nodiscard]] std::uint32_t open_saves_armed() const noexcept { return open_saves_armed_; }
    [[nodiscard]] std::uint32_t open_saves_skipped() const noexcept { return open_saves_skipped_; }

    void set_replay_save_root(std::string_view rel) noexcept {
        (void)replay_save_root_.assign(rel);
    }

    enum class RememberedFiles : std::uint8_t { AsStock, Ignore };
    void set_remembered_files(RememberedFiles r) noexcept { remembered_files_ = r; }
    [[nodiscard]] RememberedFiles remembered_files() const noexcept { return remembered_files_; }

private:
    struct MountAsk {
        UiRequest::Kind which = UiRequest::Kind::MountImage;
        proto::IoIndex index{};
        PathText path{};
        CorrelationTag tag{};
    };

    [[nodiscard]] bool order(const proto::LinkOp& op) override;
    using cores::ILadderHost::order;
    [[nodiscard]] Ex<proto::FileId> intern_bytes(std::span<const std::uint8_t> bytes,
                                                 std::string_view ext,
                                                 std::uint32_t load_addr) override;
    [[nodiscard]] Ex<proto::FileId> intern_path(std::string_view path,
                                                std::uint64_t size_bytes) override;
    [[nodiscard]] Ex<proto::FileId> intern_piece(std::span<const std::uint8_t> bytes,
                                                 std::string_view ext, std::uint64_t whole,
                                                 std::uint64_t offset) override;
    [[nodiscard]] std::size_t link_in_flight() const noexcept override {
        return inbox_.ring().size();
    }
    [[nodiscard]] std::size_t link_word_bytes() const noexcept override {
        return link_wide_ ? 2u : 1u;
    }

public:
    [[nodiscard]] bool link_wide() const noexcept { return link_wide_; }

private:
    [[nodiscard]] std::uint32_t next_generation() noexcept override;
    [[nodiscard]] cores::MountStatus mount_status() override;

    [[nodiscard]] bool order_walk(proto::IoIndex slot, std::string_view path,
                                  std::uint32_t gen) override;
    [[nodiscard]] cores::MountStatus walk_status() override { return walk_answer_; }
    [[nodiscard]] cores::SaveExtent save_extent() override;
    [[nodiscard]] proto::StatusWord status_word() override;
    void bios_missing() override;

    void on_mount_image_(const MountAsk& ask);

    [[nodiscard]] bool in_scope_(CoreScope s) const noexcept { return s.gen == conf_str_gen_; }
    [[nodiscard]] bool order_reset_(const proto::LinkOp::CoreReset& op,
                                    CorrelationTag tag) noexcept;

    void arm_mount_(const MountAsk& ask);
    void arm_ladder_(const MountAsk& ask, const cores::CoreFactory& row);

    [[nodiscard]] bool arm_start_ladder_(bool index0_taken);
    void arm_plain_mount_(const MountAsk& ask);

    void arm_next_mount_();
    void arm_start_mount_(const MountAsk& ask);
    void read_remembered_mounts_();
    [[nodiscard]] bool start_mounts_live_() const noexcept {
        return !start_mounts_.empty() || (save_mount_ && save_mount_->start) || start_pick_ladder_;
    }

    void step_save_mount_();
    void finish_save_mount_(bool ok, bool quiet = false);

    [[nodiscard]] bool arm_open_save_(const ContentRequest& cr, UiRequest::SaveChoice choice,
                                      CorrelationTag tag);
    [[nodiscard]] std::optional<PathText> open_save_path_(std::string_view rom,
                                                          UiRequest::SaveChoice choice);
    [[nodiscard]] bool prime_replay_save_(std::string_view path, bool seeded);

    [[nodiscard]] bool mount_busy_() const noexcept {
        return ladder_ != nullptr || save_mount_.has_value();
    }

    void step_ladder_();

    enum class RungWait : std::uint8_t { None, Later, Now };
    [[nodiscard]] RungWait rung_wait_() const noexcept;

    void finish_ladder_(bool ok);
    void publish_ladder_(bool live);

    void drop_ladder_();
    void forget_core_() noexcept;

    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> read_whole_(svc::IFile& f,
                                                                              std::uint64_t size);

    static constexpr std::uint32_t kMaxGen = 0xFFFFu;
    static_assert(kMaxGen <= std::numeric_limits<std::uint16_t>::max(),
                  "the gen must reach T-RT through the ApplyCore op untruncated");

    void settle_reboot_(bool cold);

    [[nodiscard]] bool watch_reboot_();

    void finish_reboot_();

    void watch_readiness_();

    void watch_start_() noexcept;
    void give_up_start_(Errc why) noexcept;

    void owe_front_end_() noexcept;
    void fall_back_to_front_end_();

    void latch_launcher_(const LauncherProfile& l, std::string_view core_name);

    [[nodiscard]] UiRequest::LoadCore launcher_alias_(
        const UiRequest::LoadCore& req) const noexcept;
    void publish_launcher_demand_(std::string_view core_name, bool direct_video) noexcept;

    void withdraw_launcher_demand_() noexcept;
    void swap_to_launcher_image_();

    void recover_session_();

    void probe_for_recovery_();
    [[nodiscard]] bool fabric_configured_() const noexcept;
    [[nodiscard]] bool fabric_unconfigured_() const noexcept;
    void sleep_ms_(unsigned ms);

    static constexpr unsigned kPushRetries = 50;
    bool push_gating_op_(const proto::LinkOp& op);

    void on_load_core_(const UiRequest::LoadCore& req, CorrelationTag tag);

    enum class Asked : std::uint8_t { Yes, Invalid, NoRoom };
    Asked ask_switch_(const UiRequest::LoadCore& req, CorrelationTag tag);

    void ask_switch_or_refuse_(const UiRequest::LoadCore& req, CorrelationTag tag);

    void retry_held_load_();

    template <class A>
    void end_switch_(A alt) noexcept;

    bool push_last_order_() noexcept;
    [[nodiscard]] bool validate_switch_(const UiRequest::LoadCore& req);

    unsigned take_park_acks_();

    void try_program_();

    void pause_seats_(std::uint32_t gen, bool at_receipt) noexcept;
    void resume_seats_() noexcept;
    [[nodiscard]] bool seats_paused_(std::uint32_t gen) const noexcept;

    [[nodiscard]] bool refuse_in_switch_(UiRequest::Kind kind, CorrelationTag tag) noexcept;
    [[nodiscard]] bool refuse_in_load_(UiRequest::Kind kind, CorrelationTag tag) noexcept;

    [[nodiscard]] const cores::CoreProfile& loaded_profile_() const;

    [[nodiscard]] bool streams_files_() const;

    void step_pieces_();

    [[nodiscard]] bool arm_loader_(const ContentRequest& req);
    [[nodiscard]] bool arm_walk_(const ContentRequest& req, const cores::CoreFactory& row);
    void step_load_();
    void step_walk_();

    void make_companion_();
    void withdraw_companion_() noexcept;
    void begin_companion_(std::string_view path);
    void step_companion_();
    void publish_window_counts_() noexcept;

    [[nodiscard]] Ex<void> attach_savestates_() noexcept;
    void order_hold_reset_() noexcept;

    template <class A>
    void order_(const A& alt) noexcept;

    template <class A>
    bool order_refusable_(const A& alt) noexcept;

    void order_bind_decoders_(proto::LinkOp::DecoderTable table, std::uint32_t gen) noexcept;

    [[nodiscard]] static proto::BindGeneration as_generation_(std::uint32_t gen) noexcept {
        return proto::BindGeneration{static_cast<std::uint16_t>(gen)};
    }

    void order_teardown_() noexcept;
    void on_identity_matched_(const proto::LinkEvent::IdentityMatched& w) noexcept;
    void on_identity_mismatch_(const proto::LinkEvent::IdentityMismatch& w) noexcept;
    void on_conf_str_(const proto::LinkEvent::ConfStr& c);

    void refuse_started_switch_(Errc why) noexcept;

    void on_core_made_(const proto::LinkEvent::CoreMade& m) noexcept;
    void order_session_up_() noexcept;

    void order_start_assets_(bool index0_taken) noexcept;

    struct RememberedFile {
        proto::ConfStrFileRow row;
        std::string path;
    };
    [[nodiscard]] std::vector<RememberedFile> read_remembered_files_() const;
    void order_remembered_files_(std::span<const RememberedFile> files);

    void order_bind_identity_(const proto::ConfStr& conf);

    void order_bind_doorbells_(const proto::ConfStr& conf);

    void order_bind_joy_(std::string_view core_name) noexcept;

    void boot_config_(std::string_view core_name, const svc::ConfigSnapshot& cfg,
                      proto::LinkOp::BindConfig& op);

    [[nodiscard]] bool wait_for_mount_(std::string_view needle);

    void publish_fabric_();

    enum class Programmed : std::uint8_t { NotNeeded, Ok, Failed };

    Programmed program_(const ParkReceipt& receipt);

    struct StepArg {
        bool loaded = false;
    };
    [[nodiscard]] StepArg step_arg_(Programmed p) const noexcept;
    Programmed program_at_boot_();
    void perform_content_(const ContentRequest& req);

    void refuse_file_ask_(UiRequest::Kind asked, CorrelationTag tag, Errc code) noexcept;
    [[nodiscard]] Ex<proto::ConfStrFileRow> resolve_load_(const UiRequest::LoadFile& r) const;
    [[nodiscard]] Ex<proto::ConfStrFileRow> resolve_load_(const UiRequest::LoadFileByDigit& r);

    template <class R>
    void load_file_(UiRequest::Kind asked, const R& req, CorrelationTag tag);

    [[nodiscard]] std::optional<ContentRequest> content_for_(const proto::ConfStrFileRow& row,
                                                             std::string_view path) const;

    void attach_row_savestates_(bool opensave);

    void order_row_content_(const ContentRequest& cr, bool opensave, UiRequest::SaveChoice choice,
                            CorrelationTag tag);

    struct PickedLoad {
        ContentRequest content{};
        bool opensave = false;
        UiRequest::SaveChoice choice = UiRequest::SaveChoice::User;
        CorrelationTag tag{};
    };

    void plan_addons_(const proto::ConfStrFileRow& row, std::string_view pick,
                      const PickedLoad& load, const cores::RamImageRecipe& image);
    void run_load_(const PickedLoad& load);
    void step_addons_();

    [[nodiscard]] bool content_ordered_() const noexcept;

    [[nodiscard]] std::optional<PathText> zip_member_(const proto::ConfStrFileRow& row,
                                                      std::string_view path) const;
    void intern_rel_at_(proto::FileId id, std::string_view rel);
    void intern_manifest_(std::string_view rel);
    void order_slot_previews_(const std::byte* pack, std::size_t len);
    void latch_mra_facts_();
    void order_bind_facts_() noexcept;
    void order_make_core_() noexcept;
    void elect_defmra_(std::string_view text);
    void intern_saved_cfg_(std::string_view core_name, proto::LinkOp::BindConfig& op);
    [[nodiscard]] proto::TxSlabId intern_exact_(std::string_view rel, std::size_t want);
    void order_bind_uart_(std::string_view core_name, const proto::ConfStr* conf = nullptr);
    void on_save_config_(CorrelationTag tag);
    [[nodiscard]] bool order_save_ask_(proto::SaveKind kind, std::uint8_t slot, CorrelationTag tag,
                                       bool gating = false) noexcept;
    void on_save_bytes_(const proto::LinkEvent::SaveBytes& e);
    void on_slot_names_(const proto::LinkEvent::SaveBytes& e);
    void finish_boot_restore_() noexcept;
    void publish_refusal_(UiRequest::Kind which, Errc code, CorrelationTag tag) noexcept;

    void advise_switch_failed_(Errc code) noexcept;
    void order_save_upload_() noexcept;
    void publish_save_verdict_(bool ok, CorrelationTag tag) noexcept;

    [[nodiscard]] bool write_durably_(std::string_view rel, std::span<const std::byte> bytes);

    QuiesceChannel* park_;
    LinkTxChannel& inbox_;
    LinkRxChannel& rx_;
    ConfigCell& config_cell_;

    proto::ConfSwitches published_conf_ = default_conf_switches();
    bool replay_ini_ = false;
    bool strict_direct_video_ = false;
    xthread::Telemetry<std::uint8_t, SeatTag::Unbound> direct_video_ini_cell_{};
    EventQueue& owner_events_;
    ConfStrCell& conf_str_cell_;
    UiRequestRing ui_requests_;
    const proto::StatusCell* status_cell_ = nullptr;
    const MountStatusCell* mount_status_cell_ = nullptr;
    const SaveExtentCell* save_extent_cell_ = nullptr;
    const hal::PinLevelCell* levels_ = nullptr;
    os::MonotonicClock default_clock_{};
    const os::IClock* clock_ = &default_clock_;

    std::unique_ptr<cores::BootLadder> ladder_;
    LadderStateCell ladder_cell_{};

    std::string ladder_last_dir_;
    bool ladder_noreset_ = false;

    bool ladder_moved_ = false;
    bool start_ladder_ = false;
    bool start_pick_ladder_ = false;
    std::optional<proto::LinkOp::CoreReset> reset_owed_{};
    std::uint32_t resets_ordered_ = 0;

    std::optional<MountAsk> held_mount_;

    struct SaveMount {
        MountAsk ask{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
        bool unmounted = false;

        bool bracketed = false;

        std::optional<ContentRequest> then_load{};
        bool start = false;
    };
    std::optional<SaveMount> save_mount_;
    std::uint32_t save_mounts_done_ = 0;
    std::uint32_t open_saves_armed_ = 0;
    std::uint32_t open_saves_skipped_ = 0;
    FixedStr<64, StrFit::Reject> replay_save_root_{};
    RememberedFiles remembered_files_ = RememberedFiles::AsStock;

    FixedStr<64, StrFit::Clip> core_name_{};
    std::uint32_t start_assets_ordered_ = 0;
    std::uint32_t remembered_ordered_ = 0;
    std::uint32_t remembered_missed_ = 0;

    std::deque<MountAsk> start_mounts_{};
    std::uint32_t remembered_mounts_armed_ = 0;
    std::uint32_t remembered_mounts_skipped_ = 0;

    std::uint8_t ladder_slot_ = 0;
    std::uint32_t mounts_armed_ = 0;
    std::uint32_t mounts_held_ = 0;
    std::uint32_t mounts_refused_ = 0;
    std::uint32_t ladders_run_ = 0;
    std::uint32_t ladder_failures_ = 0;

    std::uint32_t act_gen_ = 0;

    bool ready_watch_armed_ = false;
    bool recovering_ = false;
    std::uint32_t recover_tries_ = 0;
    std::uint32_t readiness_poll_budget_ = 0;
    std::uint32_t recoveries_ = 0;
    std::uint32_t recover_polls_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Unbound> recover_polls_cell_{};

    std::uint8_t slot_load_slot_ = 0;

    std::uint8_t slot_save_slot_ = 0;
    std::uint32_t config_saves_ = 0;
    std::uint32_t save_write_failures_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Unbound> save_write_failures_cell_{};

    PathText saved_cfg_stem_{};

    RememberedStem remembered_stem_{};
    std::uint32_t ops_posted_ = 0;
    std::uint32_t op_drops_ = 0;
    std::uint32_t identities_ok_ = 0;
    std::uint32_t identities_fail_ = 0;
    bool awaiting_identity_ = false;

    bool awaiting_conf_str_ = false;

    bool core_made_once_ = false;
    std::uint32_t identity_poll_budget_ = 0;
    std::uint32_t identity_tries_ = 0;
    std::uint32_t start_bound_ms_ = kStartBoundMs;
    os::Deadline start_due_ = os::Deadline::immediate();
    bool start_bounded_ = false;
    bool start_refused_ = false;

    bool front_end_fallback_armed_ = false;
    bool before_seats_ = false;
    bool front_end_owed_ = false;
    PathText front_end_image_{};
    LauncherDemand launcher_demand_{};
    PathText launcher_image_{};
    LauncherDemandCell* launcher_cell_ = nullptr;
    xthread::WakeFlag* launcher_wake_ = nullptr;
    bool launcher_swap_owed_ = false;
    FallbackCounts fallbacks_{};
    xthread::Telemetry<FallbackCounts, SeatTag::Unbound> fallback_cell_{};
    StepArg pending_step_arg_{};
    BitstreamProgrammer* programmer_ = nullptr;
    const svc::Vfs* vfs_ = nullptr;
    FabricCell* cell_ = nullptr;
    std::uint16_t cookie_ = 0;
    std::uint32_t fabric_gen_ = 0;
    hal::PhysRegion aperture_{os::PhysAddr{0}, 0, nullptr};
    std::uint32_t conf_str_parse_refusals_ = 0;
    std::uint32_t core_make_refusals_ = 0;

    bool boot_restore_owed_ = false;
    std::uint32_t identity_intern_refusals_ = 0;
    std::uint32_t doorbell_intern_refusals_ = 0;
    bool load_ok_ = false;
    BoardOps ops_{};
    hal::IBootHandoff* handoff_ = nullptr;
    std::uint32_t reboots_settled_ = 0;
    struct OwedReboot {
        bool cold = true;
        bool ordered = false;
        std::uint16_t seq = 0;
    };
    std::uint16_t reboot_seq_ = 0;
    std::uint32_t reboot_refusals_ = 0;
    std::optional<OwedReboot> reboot_owed_{};
    os::Deadline reboot_due_ = os::Deadline::immediate();
    std::uint32_t reboot_drain_expiries_ = 0;
    std::uint32_t order_refusals_ = 0;
    std::uint32_t gen_ = 0;
    UiRequest::LoadCore pending_{};
    CorrelationTag pending_tag_{};
    bool switch_standing_ = false;
    bool recover_owed_ = false;
    bool switch_programmed_ = false;

    struct HeldLoad {
        UiRequest::LoadCore req{};
        CorrelationTag tag{};
    };
    std::optional<HeldLoad> held_load_{};
    std::uint32_t loads_held_ = 0;
    std::uint32_t loads_cancelled_ = 0;

    std::array<xthread::PauseLatch*, hal::kThreadSeats> pauses_{};

    std::optional<ParkReceipt> receipt_{};

    std::optional<proto::LinkOp> last_order_{};
    os::Deadline pause_due_ = os::Deadline::immediate();
    std::uint32_t pause_expiries_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Unbound> pause_expiries_cell_{};
    std::uint32_t switch_belt_refusals_ = 0;
    bool reboot_failed_ = false;
    std::uint32_t load_core_refusals_ = 0;
    std::uint32_t switches_asked_ = 0;
    std::uint32_t ask_refusals_ = 0;
    std::uint32_t stale_acks_ = 0;
    std::uint32_t stale_events_ = 0;
    std::uint32_t orders_superseded_ = 0;
    std::uint32_t ui_request_unhandled_ = 0;
    std::uint32_t ui_request_misrouted_ = 0;
    std::uint32_t link_event_unhandled_ = 0;
    std::uint32_t link_event_misrouted_ = 0;
    std::uint32_t fabric_wedges_ = 0;

    std::optional<proto::ConfStr> conf_str_{};
    proto::BindGeneration conf_str_gen_{};

    bool core_live_ = false;
    std::uint32_t mgl_row0_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Unbound> mgl_row0_cell_{};
    std::uint32_t programs_run_ = 0;
    std::uint32_t programs_failed_ = 0;
    std::uint32_t files_loaded_ = 0;
    bool link_wide_ = false;
    std::uint32_t files_failed_ = 0;
    std::uint32_t ram_images_sent_ = 0;
    std::uint32_t ram_images_declined_ = 0;

    cores::RamImageRecipe pending_ram_image_{};

    std::optional<AddonSend> addons_{};
    std::optional<PickedLoad> picked_load_{};
    std::optional<AddonSend> addons_after_{};
    std::uint32_t addons_sent_ = 0;
    std::uint32_t addons_missing_ = 0;
    std::optional<FileTxPieces> pieces_{};
    std::optional<LoadLadder> load_{};
    std::optional<LoadWalk> walk_{};
    std::span<const cores::CoreWindowDecl> walk_windows_{};
    CompanionHost::Binds* companion_binds_ = nullptr;
    std::unique_ptr<cores::ICompanionLoad> companion_{};
    bool companion_attached_ = false;
    std::uint32_t companion_attach_refusals_ = 0;
    std::optional<CompanionWalk> companion_walk_{};
    std::uint16_t companion_gen_ = 0;
    std::uint32_t companion_walks_ = 0;
    std::uint32_t companion_skips_ = 0;
    std::uint32_t walk_gen_ = 0;
    cores::MountStatus walk_answer_{};
    bool walk_answer_fresh_ = false;

    cores::LoaderMemo loader_memo_{};
    ILoadWindowMap* window_map_ = nullptr;
    const FileTxLevelCell* ftx_level_cell_ = nullptr;
    LoadWindowCounts window_counts_{};
    xthread::Telemetry<LoadWindowCounts, SeatTag::Unbound> window_cell_{};
    bool made_with_manifest_ = false;
    std::uint32_t loads_refused_busy_ = 0;
    std::uint32_t savestate_refusals_ = 0;
    std::uint32_t images_sized_ = 0;
    MraFacts pending_facts_{};
    bool facts_pending_ = false;
    std::string defmra_rel_;
    bool boot_config_pending_ = true;
    std::string boot_handoff_;
};

}  // namespace mister::app
