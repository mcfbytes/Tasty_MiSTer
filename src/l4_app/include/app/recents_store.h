// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/write_verdict.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class UiWriteLane;

class RecentsStore final : public IWriteVerdict {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kMaxEntries = 16;
    static constexpr std::size_t kDirCap = 1024;
    static constexpr std::size_t kNameCap = 256;
    static constexpr std::size_t kLabelCap = 256;
    static constexpr std::size_t kRecordSize = kDirCap + kNameCap + kLabelCap;
    static constexpr std::size_t kFileSize = kMaxEntries * kRecordSize;

    class ListId {
    public:
        static constexpr proto::ListIndex kMountIndexBase{500};

        constexpr ListId() noexcept = default;

        [[nodiscard]] static constexpr ListId cores() noexcept { return ListId{}; }

        [[nodiscard]] static constexpr ListId of(proto::FileRowId row) noexcept {
            return ListId{row};
        }

        [[nodiscard]] constexpr std::optional<proto::FileRowId> row() const noexcept {
            return row_;
        }

        [[nodiscard]] std::string file_name(std::string_view core) const;

        [[nodiscard]] static std::optional<std::pair<std::string, proto::FileRowId>> from_file_name(
            std::string_view name
        );
        friend constexpr bool operator==(ListId, ListId) = default;

    private:
        constexpr explicit ListId(proto::FileRowId row) noexcept : row_(row) {}

        std::optional<proto::FileRowId> row_{};
    };

    static constexpr std::string_view kNoCore{};

    struct Entry {
        std::string dir;
        std::string name;
        std::string label;
        bool exists = false;
    };

    RecentsStore(const svc::Vfs& vfs, UiWriteLane& writes, bool enabled) noexcept
        : vfs_(&vfs),
          writes_(&writes),
          enabled_(enabled) {}

    bool enabled() const noexcept { return enabled_; }

    void set_enabled(bool e) noexcept { enabled_ = e; }

    std::vector<Entry> load(std::string_view core, ListId id);

    [[nodiscard]] Ex<void> update(
        std::string_view core,
        ListId id,
        std::string_view full_path,
        std::string_view label
    );

    [[nodiscard]] Ex<void> clear(std::string_view core, ListId id);

    static std::string file_name(std::string_view core, ListId id);

    static std::string join(std::string_view dir, std::string_view name);

    std::uint32_t loads() const noexcept { return loads_; }

    std::uint32_t updates() const noexcept { return updates_; }

    std::uint32_t errors() const noexcept { return errors_; }

    void on_written(bool durable) noexcept override;

private:
    [[nodiscard]] Ex<std::vector<std::byte>> read_raw(std::string_view core, ListId id);

    const svc::Vfs* vfs_;
    UiWriteLane* writes_;
    bool enabled_;
    std::uint32_t loads_ = 0;
    std::uint32_t updates_ = 0;
    std::uint32_t errors_ = 0;
};

}
