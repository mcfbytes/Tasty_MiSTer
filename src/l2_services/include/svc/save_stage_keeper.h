// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "proto/autosave_policy.h"
#include "proto/save_stage.h"
#include "proto/types.h"
#include "svc/card_writer.h"
#include "svc/file.h"
#include "svc/io_coworker.h"
#include "svc/save_stage_crossings.h"
#include "svc/slot_path.h"
#include "svc/staged_save.h"

namespace mister::svc {

class Vfs;

class SaveStageKeeper final : public IIoCoworker {
    TASTY_SEAT_RESIDENT(Io);

public:
    using Pool = infra::
        IndexedArray<StagedSave, proto::kMaxStagedSaves.size(), proto::SaveStageSlot>;

    struct Grant {
        proto::StageKey key{};
        std::optional<proto::FileSize> extent{};
    };

    SaveStageKeeper(
        const Vfs& vfs,
        const os::IClock& clock,
        SaveStageOrders& orders,
        SaveStageCell& report
    );
    SaveStageKeeper(const SaveStageKeeper&) = delete;
    SaveStageKeeper& operator=(const SaveStageKeeper&) = delete;
    SaveStageKeeper(SaveStageKeeper&&) = delete;
    SaveStageKeeper& operator=(SaveStageKeeper&&) = delete;
    ~SaveStageKeeper() override = default;

    [[nodiscard]] Ex<std::optional<Grant>> adopt(std::string_view path);
    [[nodiscard]] Ex<std::span<std::uint8_t>> read(
        proto::StageKey key,
        proto::FileOffset at,
        std::span<std::uint8_t> dst
    );
    [[nodiscard]] Ex<std::span<const std::uint8_t>> write(
        proto::StageKey key,
        proto::FileOffset at,
        std::span<const std::uint8_t> src
    );
    [[nodiscard]] Ex<proto::FileSize> create(
        proto::StageKey key,
        std::span<const std::uint8_t> first
    );

    void retire(proto::StageKey key) noexcept;

    [[nodiscard]] Ex<void> demote(proto::StageKey key);

    void note_in_place_opened() noexcept;
    void note_in_place_closed() noexcept;

    static constexpr LaneClass kLaneClass = LaneClass::Bulk;
    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;
    [[nodiscard]] bool quiescent() const noexcept override;
    void on_pause() noexcept override;
    void on_resume() noexcept override;

    void on_stop() noexcept override;

    void on(const proto::SaveStageOrder::Commit& c) noexcept;
    void on(const proto::SaveStageOrder::CommitAll& a) noexcept;
    void misrouted(const proto::SaveStageOrder& o) noexcept;

    [[nodiscard]] proto::SaveStageReport report() const noexcept;

    [[nodiscard]] const Pool& pool() const noexcept { return pool_; }

    static constexpr std::string_view kTempName = ".tasty-save.tmp";

    static constexpr std::string_view kBackupSuffix = ".bak";

private:
    enum class Step : std::uint8_t { Compare, Backup, Write, WriteInPlace, Replace };

    struct Job {
        proto::SaveStageSlot slot{};
        proto::StageRevision rev{};
        proto::SaveBackup backup = proto::SaveBackup::None;
        Step step = Step::Write;
        proto::FileSize size{};
        proto::FileSize card{};
        proto::FileOffset at{};
        std::unique_ptr<IFile> src{};
        std::unique_ptr<IFile> dst{};
        SlotPathText temp{};
    };

    [[nodiscard]] infra::OptRef<StagedSave> staged_(proto::StageKey key) noexcept;
    [[nodiscard]] std::optional<proto::SaveStageSlot> free_slot_() const noexcept;
    [[nodiscard]] bool live_path_(std::string_view path) const noexcept;
    [[nodiscard]] std::optional<proto::SaveStageSlot> next_unsaved_(
        std::optional<proto::SaveStageSlot> after
    ) const noexcept;
    [[nodiscard]] bool loading_() const noexcept;
    [[nodiscard]] bool busy_() const noexcept;
    void step_one_load_() noexcept;
    void take_order_(const proto::SaveStageOrder& o) noexcept;
    void start_(proto::SaveStageSlot slot, proto::SaveBackup backup) noexcept;
    void step_job_() noexcept;
    void step_compare_(Job& j, StagedSave& st) noexcept;
    void step_backup_(Job& j, StagedSave& st) noexcept;
    void step_write_(Job& j, StagedSave& st) noexcept;
    void step_in_place_(Job& j, StagedSave& st) noexcept;
    void step_replace_(Job& j, StagedSave& st) noexcept;
    static void after_compare_(Job& j) noexcept;
    static void to_write_(Job& j) noexcept;
    void skip_backup_(Job& j) noexcept;
    void write_refused_(Job& j, StagedSave& st, const Error& e) noexcept;
    [[nodiscard]] std::optional<Grant> decline_() noexcept;
    void land_(StagedSave& st) noexcept;
    void fail_(StagedSave& st, const Error& e) noexcept;
    void end_job_() noexcept;

    void sweep_from_(std::optional<proto::SaveStageSlot> after) noexcept;
    void finish_inline_() noexcept;
    void sweep_inline_() noexcept;
    void publish_() noexcept;

    const Vfs& vfs_;
    CardWriter card_;
    const os::IClock& clock_;
    SaveStageOrders& orders_;
    SaveStageCell& cell_;
    Pool pool_{};
    std::vector<std::uint8_t> commit_buf_;
    std::vector<std::uint8_t> chunk_buf_;
    std::optional<Job> job_{};
    std::optional<proto::CommitAllSeq> sweep_{};
    std::optional<StagedSave::TimePoint> paused_at_{};
    proto::SaveStageReport::Summary summary_{};
};

}
