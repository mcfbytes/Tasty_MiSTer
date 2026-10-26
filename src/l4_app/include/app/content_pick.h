// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/act_keys.h"
#include "app/addon_send.h"
#include "app/boot_restore.h"
#include "app/companion_walk.h"
#include "app/content_request.h"
#include "app/core_scope.h"
#include "app/event.h"
#include "app/file_tx_level.h"
#include "app/file_tx_pieces.h"
#include "app/interned_file.h"
#include "app/link_tx_channel.h"
#include "app/load_key.h"
#include "app/load_ladder.h"
#include "app/load_walk.h"
#include "app/load_window.h"
#include "app/load_window_map.h"
#include "app/owner_tally.h"
#include "app/path_text.h"
#include "app/picked_load.h"
#include "app/rung_set.h"
#include "app/stream_io.h"
#include "app/types.h"
#include "app/ui_request.h"
#include "app/window_tally.h"
#include "cores/core_window_decl.h"
#include "cores/file_slot.h"
#include "cores/loader_context.h"
#include "cores/loader_memo.h"
#include "cores/ram_image.h"
#include "cores/registry.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/telemetry.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "proto/conf_str.h"
#include "proto/link_op.h"
#include "proto/status_cell.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::svc {
class Vfs;
struct DirEntry;
}

namespace mister::app {

class MountQueue;
class NegotiatedCore;
class OwnerLadder;

class ContentPick {
    TASTY_SEAT_RESIDENT(Main);

public:
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

    struct Owed {
        UiRequest::Kind which{};
        CorrelationTag tag{};
    };

    struct Out {
        std::optional<Errc> refuse{};
        std::optional<PickedLoad> run{};
    };

    struct After {
        std::optional<PathText> companion{};
    };

    class Host {
    public:
        [[nodiscard]] virtual bool arm_open_save(
            const ContentRequest& cr,
            UiRequest::SaveChoice choice,
            CorrelationTag tag
        ) = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        virtual ~Host() = default;
    };

    struct Tally {
        std::uint32_t long_wait_resumes = 0;
        std::uint32_t long_wait_refusals = 0;
        std::uint32_t long_wait_drops = 0;
    };

    using Count = xthread::Telemetry<std::uint32_t, SeatTag::Main>;

    struct Wiring {
        LinkTxChannel<SeatTag::Main>& inbox;
        const FileTxLevelCell& ftx_level_cell;
        EventQueue<SeatTag::Main>& events;
        const svc::Vfs& vfs;
        ILoadWindowMap<seat_owner::Main>& window_map;
        const NegotiatedCore& negotiated;
        const MountQueue& mounts;
        const OwnerLadder& ladder;
        RungSet& rungs;
        OwnerTally& owner_tally;
        Count& mgl_row0_cell;
        ActKeys& act_keys;
        const proto::StatusCell& status_cell;
        const BootRestore& boot_restore;
        hal::PhysRegion aperture;
        WindowTally& windows;
    };

    explicit ContentPick(const Wiring& w) noexcept;

    ContentPick(const ContentPick&) = delete;
    ContentPick& operator=(const ContentPick&) = delete;

    template <class R>
    [[nodiscard]] Out load_file(
        UiRequest::Kind asked,
        const R& req,
        CorrelationTag tag,
        const cores::RamImageRecipe& ram_image
    );

    [[nodiscard]] After run_attached_load(const PickedLoad& load, Host& host);

    [[nodiscard]] After order_row_content(
        const ContentRequest& cr,
        bool opensave,
        UiRequest::SaveChoice choice,
        CorrelationTag tag,
        Host& host
    );

    [[nodiscard]] After launch_content(const ContentRequest& cr);
    [[nodiscard]] After perform_content(const ContentRequest& req);

    [[nodiscard]] bool arm_loader(
        const ContentRequest& req,
        std::string_view save = {},
        std::uint64_t save_bytes = 0
    );

    [[nodiscard]] std::optional<ContentRequest> content_for(
        const proto::ConfStrFileRow& row,
        std::string_view path
    ) const;

    [[nodiscard]] static std::optional<PathText> bare_zip_member(
        std::string_view path,
        std::string_view exts,
        std::span<const svc::DirEntry> listing
    );

    [[nodiscard]] bool order_content_tx(proto::FileId file, ContentWatch watch);

    void step_watch();

    void forget_watch() noexcept;

    void watch_last_piece();
    [[nodiscard]] bool watch_answered() const noexcept;

    void drop_watch() noexcept {
        TASTY_SEAT_BODY(ContentPick);
        watch_.reset();
    }

    [[nodiscard]] bool content_watched() const noexcept { return watch_.has_value(); }

    [[nodiscard]] Owed& content_owed() noexcept { return content_owed_; }

    [[nodiscard]] const Owed& content_owed() const noexcept { return content_owed_; }

    [[nodiscard]] std::optional<PickedLoad>& picked_load() noexcept { return picked_load_; }

    [[nodiscard]] const std::optional<PickedLoad>& picked_load() const noexcept {
        return picked_load_;
    }

    [[nodiscard]] cores::LoaderMemo& loader_memo() noexcept { return loader_memo_; }

    [[nodiscard]] const cores::LoaderMemo& loader_memo() const noexcept { return loader_memo_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    [[nodiscard]] Ex<proto::ConfStrFileRow> resolve_load_(const UiRequest::LoadFile& r) const;
    [[nodiscard]] Ex<proto::ConfStrFileRow> resolve_load_(const UiRequest::LoadFileByDigit& r);

    void plan_addons_(
        const proto::ConfStrFileRow& row,
        std::string_view pick,
        const PickedLoad& load,
        const cores::RamImageRecipe& image
    );

    [[nodiscard]] bool arm_walk_(const ContentRequest& req, const cores::CoreFactory& row);

    [[nodiscard]] std::optional<PathText> zip_member_(
        const proto::ConfStrFileRow& row,
        std::string_view path
    ) const;

    [[nodiscard]] static bool wants_zip_member(std::string_view path, std::string_view exts);

    [[nodiscard]] bool streams_files_() const;

    [[nodiscard]] bool no_mount_in_flight_() const noexcept;
    void publish_done_(UiRequest::Kind which, CorrelationTag tag) noexcept;
    LinkTxChannel<SeatTag::Main>& inbox_;
    const FileTxLevelCell& ftx_level_cell_;
    EventQueue<SeatTag::Main>& owner_events_;
    const svc::Vfs& vfs_;
    ILoadWindowMap<seat_owner::Main>& window_map_;
    const NegotiatedCore& negotiated_;
    const MountQueue& mounts_;
    const OwnerLadder& ladder_;
    RungSet& rungs_;
    OwnerTally& owner_tally_;
    Count& mgl_row0_cell_;
    ActKeys& act_keys_;
    const proto::StatusCell& status_cell_;
    const BootRestore& boot_restore_;
    const hal::PhysRegion aperture_;
    WindowTally& windows_;
    Owed content_owed_{};
    std::optional<PickedLoad> picked_load_{};
    std::optional<ContentWatch> watch_{};

    cores::LoaderMemo loader_memo_{};
    Tally tally_{};
};

}
