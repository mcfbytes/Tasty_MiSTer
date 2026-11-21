// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "os/raw_monotonic_clock.h"
#include "proto/save_stage.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/slot_path.h"

namespace mister::svc {

class StagedSave {
    TASTY_SEAT_RESIDENT(Io);

public:
    using TimePoint = os::RawMonotonicClock::time_point;

    StagedSave() = default;
    StagedSave(const StagedSave&) = delete;
    StagedSave& operator=(const StagedSave&) = delete;
    StagedSave(StagedSave&&) noexcept = default;
    StagedSave& operator=(StagedSave&&) noexcept = default;
    ~StagedSave() = default;

    void take_absent(proto::StageKey key, std::string_view path);

    void take_loading(
        proto::StageKey key,
        std::string_view path,
        std::unique_ptr<IFile> file,
        proto::FileSize size
    );

    void step_load() noexcept;

    void finish_load() noexcept;
    void retire() noexcept;
    void revive() noexcept;

    void release() noexcept;

    void saved_as(proto::StageRevision rev) noexcept;

    void found_identical(proto::StageRevision rev) noexcept;

    void note_fault(const Error& e) noexcept {
        faults_.add();
        last_fault_ = e;
    }

    void note_decline() noexcept { declines_.add(); }

    [[nodiscard]] Ex<infra::ByteCount> read(proto::FileOffset at, std::span<std::byte> dst);

    [[nodiscard]] Ex<infra::ByteCount> write(
        proto::FileOffset at,
        std::span<const std::byte> src,
        TimePoint now
    );

    [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::byte> first, TimePoint now);

    [[nodiscard]] static Error too_large(infra::ErrorSite at) noexcept;
    [[nodiscard]] static bool is_too_large(const Error& e) noexcept;

    [[nodiscard]] proto::StageKey key() const noexcept { return key_; }

    [[nodiscard]] proto::StageState state() const noexcept { return state_; }

    [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

    [[nodiscard]] proto::StageRevision rev() const noexcept { return rev_; }

    [[nodiscard]] proto::StageRevision saved() const noexcept { return saved_; }

    [[nodiscard]] bool unsaved() const noexcept { return rev_ != saved_; }

    [[nodiscard]] bool present() const noexcept { return present_; }

    [[nodiscard]] bool holds(proto::StageKey key) const noexcept;

    [[nodiscard]] bool resident() const noexcept;

    [[nodiscard]] std::span<const std::byte> image() const noexcept { return image_; }

    [[nodiscard]] proto::FileSize extent() const noexcept;

    [[nodiscard]] std::optional<proto::FileSize> announced() const noexcept;
    [[nodiscard]] proto::SaveStageRow row() const noexcept;

private:
    void take_(proto::StageKey key, std::string_view path);
    void break_(Error e) noexcept;

    std::vector<std::byte> image_{};
    proto::StageState state_ = proto::StageState::Free;
    proto::StageKey key_{};
    proto::StageRevision rev_{};
    proto::StageRevision saved_{};
    bool present_ = false;
    TimePoint last_write_{};
    infra::Tally commits_{};
    infra::Tally skipped_identical_{};
    infra::Tally faults_{};
    std::optional<Error> last_fault_{};
    infra::Tally declines_{};
    SlotPathText path_{};
    std::unique_ptr<IFile> load_{};
    proto::FileOffset loaded_{};
    std::optional<Error> broken_{};
};

}
