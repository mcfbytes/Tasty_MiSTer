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
#include "app/board_reboot.h"
#include "app/bitstream_programmer.h"
#include "app/cheat_catalog_cell.h"
#include "app/conf_str_cell.h"
#include "app/core_builder.h"
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
#include "app/manifest_ladder.h"
#include "app/mra_facts.h"
#include "app/path_text.h"
#include "app/pending_load.h"
#include "app/remembered_path.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "app/start_chain.h"
#include "app/ui_request.h"
#include "app/ui_request_ring.h"
#include "cores/companion_load.h"
#include "cores/loader_memo.h"
#include "cores/boot_ladder.h"
#include "cores/ladder_host.h"
#include "infra/pause_latch.h"
#include "app/launcher_demand.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
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

template <SeatTag Producer>
class EventQueue;

class SessionOwner : private cores::ILadderHost {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Link {
        LinkTxChannel<SeatTag::Main>& inbox;
        LinkRxChannel<SeatTag::Main>& rx;
        ConfigCell& config_cell;
        EventQueue<SeatTag::Main>& owner_events;
        ConfStrCell& conf_str_cell;

        CheatCatalogCell& cheat_catalog_cell;

        LadderStateCell& ladder_cell;

        xthread::WakeFlag& main_wake;

        BoardOps ops{};

        infra::OptRef<hal::IBootHandoff> boot_handoff{};
        infra::OptRef<LauncherDemandCell> launcher_demand{};
        infra::OptRef<xthread::WakeFlag> launcher_wake{};

        BitstreamProgrammer& programmer;
        const svc::Vfs& storage;
        ILoadWindowMap<seat_owner::Main>& window_map;

        FabricCell& fabric_cell;

        const proto::StatusCell& status_cell;
        const MountStatusCell& mount_status_cell;
        const SaveExtentCell& save_extent_cell;
        const FileTxLevelCell& file_tx_level_cell;

        const hal::PinLevelCell& pin_levels;

        infra::OptRef<const os::IClock> clock{};

        infra::OptRef<SaveStateAttach::AskInbox> save_state_asks{};
        infra::OptRef<const SaveStateFactsCell> save_state_facts{};

        CoreBay& bay;
        const BayReleasesCell& bay_releases;
        const CoreTerms& core_terms;

        infra::OptRef<hal::IDoorbellSource> doorbell_source{};
    };

    SessionOwner(QuiesceChannel& park, const Link& link) noexcept
        : park_(park), inbox_(link.inbox), rx_(link.rx), config_cell_(link.config_cell),
          owner_events_(link.owner_events), conf_str_cell_(link.conf_str_cell),
          cheat_catalog_cell_(link.cheat_catalog_cell), ui_requests_(link.main_wake),
          status_cell_(link.status_cell), mount_status_cell_(link.mount_status_cell),
          save_extent_cell_(link.save_extent_cell), levels_(link.pin_levels),
          clock_(link.clock.has_value() ? static_cast<const os::IClock&>(*link.clock)
                                        : static_cast<const os::IClock&>(default_clock_)),
          ladder_cell_(link.ladder_cell), programmer_(link.programmer), vfs_(link.storage),
          cell_(link.fabric_cell), ops_(link.ops), window_map_(link.window_map),
          ftx_level_cell_(link.file_tx_level_cell), save_state_asks_(link.save_state_asks),
          save_state_facts_(link.save_state_facts),
          builder_(link.bay, link.bay_releases, link.core_terms, link.doorbell_source),
          reboot_(link.boot_handoff, link.ops.reset, link.ops.sleeper) {
        (void)front_end_image_.assign(kFrontEndImage);
        adopt_(link);
    }

    SessionOwner(const SessionOwner&) = delete;
    SessionOwner& operator=(const SessionOwner&) = delete;

    void set_identity_poll_budget(std::uint32_t n) noexcept { identity_poll_budget_ = n; }

    static constexpr std::uint32_t kStartBoundMs = 5000;
    void set_start_bound_ms(std::uint32_t ms) noexcept { start_bound_ms_ = ms; }

    bool set_front_end_image(std::string_view rel) noexcept { return front_end_image_.assign(rel); }

    void set_readiness_poll_budget(std::uint32_t n) noexcept { readiness_poll_budget_ = n; }

    [[nodiscard]] infra::OptRef<const LauncherProfile> launcher() const noexcept {
        return launcher_demand_.profile;
    }

    void set_aperture(hal::PhysRegion region) noexcept { aperture_ = region; }

    [[nodiscard]] const LadderStateCell& ladder_cell() const noexcept { return ladder_cell_; }

    void grant_pause(SeatTag s, xthread::PauseLatch& latch) noexcept {
        pauses_[hal::row_index(s)] = &latch;
    }

    [[nodiscard]] bool program_owed() const noexcept;

    [[nodiscard]] int wake_hint_ms(int floor_ms) const noexcept;

    [[nodiscard]] std::uint32_t pause_expiries() const noexcept { return pause_expiries_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>& pause_expiries_cell()
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

    [[nodiscard]] const xthread::Telemetry<FallbackCounts, SeatTag::Main>& fallback_cell()
        const noexcept {
        return fallback_cell_;
    }

    [[nodiscard]] std::uint32_t switch_belt_refusals() const noexcept {
        return switch_belt_refusals_;
    }

    [[nodiscard]] std::uint32_t reboot_refusals() const noexcept { return reboot_refusals_; }

    [[nodiscard]] bool load_live() const noexcept { return load_.has_value(); }
    [[nodiscard]] const LoadLadder* load() const noexcept { return load_ ? &*load_ : nullptr; }

    [[nodiscard]] const LoadWalk* walk() const noexcept { return walk_ ? &*walk_ : nullptr; }

    void set_companion_binds(CompanionHost::Binds* b) noexcept {
        companion_binds_ = infra::opt_ref_of(b);
    }

    [[nodiscard]] const CompanionWalk* companion_walk() const noexcept {
        return companion_walk_ ? &*companion_walk_ : nullptr;
    }
    [[nodiscard]] std::uint32_t companion_walks() const noexcept { return companion_walks_; }
    [[nodiscard]] std::uint32_t companion_skips() const noexcept { return companion_skips_; }

    [[nodiscard]] std::uint32_t companion_attach_refusals() const noexcept {
        return companion_attach_refusals_;
    }
    [[nodiscard]] const LoadWindowCounts& window_counts() const noexcept { return window_counts_; }

    [[nodiscard]] const ManifestLadder* manifest_walk() const noexcept {
        return manifest_ ? &*manifest_ : nullptr;
    }

    [[nodiscard]] std::uint32_t manifest_errors() const noexcept { return manifest_errors_; }

    [[nodiscard]] std::string_view manifest_text_for_test() const noexcept {
        return manifest_text_();
    }
    [[nodiscard]] const xthread::Telemetry<LoadWindowCounts, SeatTag::Main>& window_cell()
        const noexcept {
        return window_cell_;
    }

    [[nodiscard]] bool pieces_live() const noexcept { return pieces_.has_value(); }

    [[nodiscard]] bool content_watched() const noexcept { return watch_.has_value(); }

    [[nodiscard]] bool rung_pending() const noexcept;

    [[nodiscard]] std::uint32_t loads_refused_busy() const noexcept { return loads_refused_busy_; }

    [[nodiscard]] std::uint32_t savestate_refusals() const noexcept { return savestate_refusals_; }

    [[nodiscard]] bool savestate_held() const noexcept { return ss_hold_.has_value(); }

    [[nodiscard]] std::uint32_t savestate_hold_expiries() const noexcept {
        return savestate_hold_expiries_;
    }

    void set_boot_cookie(std::uint16_t c) {
        cookie_ = c;
        publish_fabric_();
    }

    unsigned tick();

    void service_boot(const UiRequest::LoadCore& req);

    void expect_boot_negotiate() noexcept;

    void latch_boot_companion(std::string_view xml) noexcept;

    [[nodiscard]] const CoreBuilder& core_builder() const noexcept { return builder_; }

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
    void on(const UiRequest::SetOsdStyle& req, const UiRequest::Head& head);
    void on(const UiRequest::SdramDetected& req, const UiRequest::Head& head);
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

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>& recover_polls_cell()
        const noexcept {
        return recover_polls_cell_;
    }

    [[nodiscard]] std::uint32_t reboots_settled() const noexcept { return reboots_settled_; }

    [[nodiscard]] BoardReboot& board_reboot() noexcept { return reboot_; }

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
    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>& mgl_row0_cell()
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

    [[nodiscard]] std::uint32_t boot_images_armed() const noexcept { return boot_images_armed_; }
    [[nodiscard]] std::uint32_t boot_images_skipped() const noexcept {
        return boot_images_skipped_;
    }
    [[nodiscard]] std::uint32_t remembered_missed() const noexcept { return remembered_missed_; }
    [[nodiscard]] std::uint32_t files_failed() const noexcept { return files_failed_; }

    [[nodiscard]] std::uint32_t long_wait_resumes() const noexcept { return long_wait_resumes_; }
    [[nodiscard]] std::uint32_t long_wait_refusals() const noexcept { return long_wait_refusals_; }

    [[nodiscard]] std::uint32_t long_wait_drops() const noexcept { return long_wait_drops_; }

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

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>& save_write_failures_cell()
        const noexcept {
        return save_write_failures_cell_;
    }

    [[nodiscard]] std::uint32_t ini_write_failures() const noexcept { return ini_write_failures_; }

    [[nodiscard]] const xthread::Telemetry<std::uint32_t, SeatTag::Main>& ini_write_failures_cell()
        const noexcept {
        return ini_write_failures_cell_;
    }

    [[nodiscard]] std::uint32_t memsz_unmeasured() const noexcept { return memsz_unmeasured_; }

    [[nodiscard]] std::uint32_t sdram_write_failures() const noexcept {
        return sdram_write_failures_;
    }

    [[nodiscard]] LinkTxChannel<SeatTag::Main>& inbox() noexcept { return inbox_; }
    [[nodiscard]] const LinkTxChannel<SeatTag::Main>& inbox() const noexcept { return inbox_; }
    [[nodiscard]] const proto::LinkTxRing& tx() const noexcept { return inbox_.ring(); }
    [[nodiscard]] std::uint32_t ops_posted() const noexcept { return ops_posted_; }
    [[nodiscard]] std::uint32_t op_drops() const noexcept { return op_drops_; }

    [[nodiscard]] LinkRxChannel<SeatTag::Main>& rx() noexcept { return rx_; }
    [[nodiscard]] const LinkRxChannel<SeatTag::Main>& rx() const noexcept { return rx_; }
    [[nodiscard]] ConfigCell& config_cell() noexcept { return config_cell_; }

    [[nodiscard]] bool boot_config_parsed() const noexcept { return !boot_config_pending_; }

    [[nodiscard]] const std::string& boot_handoff() const noexcept { return boot_handoff_; }

    void arm_replay_ini(bool strict) noexcept;

    [[nodiscard]] const xthread::Telemetry<std::uint8_t, SeatTag::Main>& direct_video_ini_cell()
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

    [[nodiscard]] bool park_acks_empty() const noexcept { return park_.ack_empty(); }

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
        std::optional<std::uint8_t> index_word{};
    };

    struct Owed {
        UiRequest::Kind which{};
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
        TASTY_SEAT_BODY(SessionOwner);
        return inbox_.ring().size();
    }
    [[nodiscard]] std::size_t link_word_bytes() const noexcept override {
        TASTY_SEAT_BODY(SessionOwner);
        return link_wide_ ? 2u : 1u;
    }

public:
    [[nodiscard]] bool link_wide() const noexcept { return link_wide_; }

private:
    [[nodiscard]] std::uint32_t next_generation() noexcept override;
    [[nodiscard]] cores::MountStatus mount_status() override;

    [[nodiscard]] bool order_walk(proto::IoIndex slot, std::string_view path,
                                  std::uint32_t gen) override;
    [[nodiscard]] cores::MountStatus walk_status() override {
        TASTY_SEAT_BODY(SessionOwner);
        return walk_answer_;
    }

    [[nodiscard]] bool order_savestates(std::string_view image, std::uint32_t gen) override;
    [[nodiscard]] std::uint32_t savestates_answered() override;
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

    [[nodiscard]] bool read_remembered_mounts_();

    [[nodiscard]] std::optional<std::uint8_t> pick_index_word_(proto::IoIndex index,
                                                               std::string_view path) const;
    void queue_boot_images_(const StartChain& chain);
    void arm_boot_image_(const MountAsk& ask);
    [[nodiscard]] bool start_mounts_live_() const noexcept {
        return !start_mounts_.empty() || !boot_images_.empty() ||
               (save_mount_ && save_mount_->start) || start_pick_ladder_;
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

    void note_memsz_(const UiRequest::LoadCore& req) noexcept;

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

    struct ContentWatch {
        std::uint32_t act = 0;
        std::unique_ptr<svc::IFile> file{};
        std::uint64_t total = 0;
        std::uint32_t crc = 0;
        std::uint8_t wire_index = 0;
        PathText path{};
        std::uint64_t from = 0;
        bool cut_on_hold = false;
    };

    [[nodiscard]] bool order_content_tx_(proto::FileId file, ContentWatch watch);

    void step_watch_();

    void forget_watch_() noexcept;

    void watch_last_piece_();
    [[nodiscard]] bool watch_answered_() const noexcept;

    [[nodiscard]] bool arm_loader_(const ContentRequest& req, std::string_view save = {},
                                   std::uint64_t save_bytes = 0);

    [[nodiscard]] const cores::CoreFactory* load_row_() const;
    [[nodiscard]] bool owns_loader_() const;
    [[nodiscard]] bool arm_walk_(const ContentRequest& req, const cores::CoreFactory& row);
    void step_load_();
    void step_walk_();

    void make_companion_();
    void withdraw_companion_() noexcept;
    void begin_companion_(std::string_view path);
    void step_companion_();
    void publish_window_counts_() noexcept;

    [[nodiscard]] bool ask_savestates_(std::string_view rom, std::uint32_t gen) noexcept;
    [[nodiscard]] bool savestates_answered_(std::uint32_t gen) const noexcept;
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

    [[nodiscard]] StartChain::Facts start_facts_(std::string_view rbf, bool index0_taken,
                                                 bool slot0_held, bool remembered,
                                                 bool ladder) const;

    [[nodiscard]] std::string start_rbf_() const;

    void order_start_chain_(const StartChain& chain);

    void order_rbf_tail_(std::string_view path, proto::WideIoIndex dest, std::uint64_t offset);

    [[nodiscard]] bool order_reset_level_(bool asserted);

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

    void adopt_(const Link& link);

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

    [[nodiscard]] std::optional<std::uint32_t> attach_row_savestates_(bool opensave,
                                                                      std::string_view rom);

    void order_row_content_(const ContentRequest& cr, bool opensave, UiRequest::SaveChoice choice,
                            CorrelationTag tag);

    void launch_content_(const ContentRequest& cr);

    struct PickedLoad {
        ContentRequest content{};
        bool opensave = false;
        UiRequest::SaveChoice choice = UiRequest::SaveChoice::User;
        CorrelationTag tag{};
    };

    void plan_addons_(const proto::ConfStrFileRow& row, std::string_view pick,
                      const PickedLoad& load, const cores::RamImageRecipe& image);
    void run_load_(const PickedLoad& load);

    void run_attached_load_(const PickedLoad& load);
    void step_savestate_hold_();
    void step_addons_();

    [[nodiscard]] bool content_ordered_() const noexcept;

    [[nodiscard]] std::optional<PathText> zip_member_(const proto::ConfStrFileRow& row,
                                                      std::string_view path) const;
    [[nodiscard]] bool intern_rel_at_(proto::FileId id, std::string_view rel);
    void intern_manifest_(std::string_view rel);
    [[nodiscard]] std::vector<std::uint8_t> read_manifest_(std::string_view rel) const;
    void pack_manifest_cheats_(std::span<const std::uint8_t> doc);
    void order_slot_previews_(const std::byte* pack, std::size_t len);
    void latch_mra_facts_();
    void order_bind_facts_() noexcept;
    void order_make_core_();
    [[nodiscard]] std::string_view make_core_rel_() const noexcept;
    void post_make_core_();

    [[nodiscard]] bool arm_manifest_();
    void step_manifest_();
    void finish_core_made_() noexcept;
    [[nodiscard]] std::string_view manifest_text_() const noexcept;
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

    void publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept;

    void advise_switch_failed_(Errc code) noexcept;
    void order_save_upload_() noexcept;
    void publish_save_verdict_(bool ok, CorrelationTag tag) noexcept;

    [[nodiscard]] bool write_durably_(std::string_view rel, std::span<const std::byte> bytes);

    QuiesceChannel& park_;
    LinkTxChannel<SeatTag::Main>& inbox_;
    LinkRxChannel<SeatTag::Main>& rx_;
    ConfigCell& config_cell_;

    proto::ConfSwitches published_conf_ = default_conf_switches();
    bool replay_ini_ = false;
    bool strict_direct_video_ = false;
    xthread::Telemetry<std::uint8_t, SeatTag::Main> direct_video_ini_cell_{};
    EventQueue<SeatTag::Main>& owner_events_;
    ConfStrCell& conf_str_cell_;
    CheatCatalogCell& cheat_catalog_cell_;

    CheatCatalog cheat_catalog_{};
    UiRequestRing ui_requests_;
    const proto::StatusCell& status_cell_;
    const MountStatusCell& mount_status_cell_;
    const SaveExtentCell& save_extent_cell_;
    const hal::PinLevelCell& levels_;
    os::MonotonicClock default_clock_{};
    const os::IClock& clock_;

    std::unique_ptr<cores::BootLadder> ladder_;
    LadderStateCell& ladder_cell_;

    std::string ladder_last_dir_;
    bool ladder_noreset_ = false;

    bool ladder_moved_ = false;
    bool start_ladder_ = false;
    bool start_pick_ladder_ = false;
    Owed ladder_owed_{};
    Owed content_owed_{};
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

    bool generic_identity_ = false;
    std::uint32_t remembered_ordered_ = 0;
    std::uint32_t remembered_missed_ = 0;

    std::deque<MountAsk> start_mounts_{};
    std::uint32_t remembered_mounts_armed_ = 0;
    std::uint32_t remembered_mounts_skipped_ = 0;

    std::deque<MountAsk> boot_images_{};
    std::uint32_t boot_images_armed_ = 0;
    std::uint32_t boot_images_skipped_ = 0;

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
    xthread::Telemetry<std::uint32_t, SeatTag::Main> recover_polls_cell_{};

    std::uint8_t slot_load_slot_ = 0;

    std::uint8_t slot_save_slot_ = 0;
    std::uint32_t config_saves_ = 0;
    std::uint32_t save_write_failures_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Main> save_write_failures_cell_{};
    std::uint32_t ini_write_failures_ = 0;
    xthread::Telemetry<std::uint32_t, SeatTag::Main> ini_write_failures_cell_{};
    std::uint32_t memsz_unmeasured_ = 0;
    std::uint32_t sdram_write_failures_ = 0;

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
    infra::OptRef<LauncherDemandCell> launcher_cell_{};
    infra::OptRef<xthread::WakeFlag> launcher_wake_{};
    bool launcher_swap_owed_ = false;
    FallbackCounts fallbacks_{};
    xthread::Telemetry<FallbackCounts, SeatTag::Main> fallback_cell_{};
    StepArg pending_step_arg_{};
    BitstreamProgrammer& programmer_;
    const svc::Vfs& vfs_;
    FabricCell& cell_;
    std::uint16_t cookie_ = 0;
    std::uint32_t fabric_gen_ = 0;
    hal::PhysRegion aperture_{os::PhysAddr{0}, 0, nullptr};
    std::uint32_t conf_str_parse_refusals_ = 0;
    std::uint32_t core_make_refusals_ = 0;

    bool boot_restore_owed_ = false;
    std::uint32_t identity_intern_refusals_ = 0;
    std::uint32_t doorbell_intern_refusals_ = 0;
    bool load_ok_ = false;
    const BoardOps ops_{};
    infra::OptRef<hal::IBootHandoff> handoff_{};
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
    xthread::Telemetry<std::uint32_t, SeatTag::Main> pause_expiries_cell_{};
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
    xthread::Telemetry<std::uint32_t, SeatTag::Main> mgl_row0_cell_{};
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

    struct SaveStateHold {
        PickedLoad load{};
        std::uint32_t gen = 0;
        std::int64_t due_ns = 0;
    };
    std::optional<SaveStateHold> ss_hold_{};
    std::uint32_t addons_sent_ = 0;
    std::uint32_t addons_missing_ = 0;
    std::optional<FileTxPieces> pieces_{};
    std::optional<ContentWatch> watch_{};
    std::uint32_t long_wait_resumes_ = 0;
    std::uint32_t long_wait_refusals_ = 0;
    std::uint32_t long_wait_drops_ = 0;
    std::optional<LoadLadder> load_{};
    std::optional<ManifestLadder> manifest_{};
    bool made_restore_owed_ = false;
    bool made_cheat_records_ = false;
    std::uint32_t manifest_errors_ = 0;
    std::optional<LoadWalk> walk_{};
    std::span<const cores::CoreWindowDecl> walk_windows_{};
    infra::OptRef<CompanionHost::Binds> companion_binds_{};
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
    ILoadWindowMap<seat_owner::Main>& window_map_;
    const FileTxLevelCell& ftx_level_cell_;
    infra::OptRef<SaveStateAttach::AskInbox> save_state_asks_;
    infra::OptRef<const SaveStateFactsCell> save_state_facts_;

    CoreBuilder builder_;

    BoardReboot reboot_;
    LoadWindowCounts window_counts_{};
    xthread::Telemetry<LoadWindowCounts, SeatTag::Main> window_cell_{};
    bool made_with_manifest_ = false;
    PathText manifest_rel_{};
    std::vector<std::uint8_t> manifest_doc_{};
    PathText boot_companion_{};
    std::uint32_t loads_refused_busy_ = 0;
    std::uint32_t savestate_refusals_ = 0;
    std::uint32_t savestate_hold_expiries_ = 0;
    std::uint32_t images_sized_ = 0;
    MraFacts pending_facts_{};
    bool facts_pending_ = false;
    std::string defmra_rel_;
    bool boot_config_pending_ = true;
    std::string boot_handoff_;
};

}  // namespace mister::app
