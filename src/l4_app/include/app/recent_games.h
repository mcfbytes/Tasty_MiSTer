// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/played_history.h"
#include "app/recents_store.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::os {
class IClock;
}
namespace mister::svc {
class Vfs;
}

namespace mister::app {

class RecentGames {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Played {
        std::string_view core_name{};
        std::string_view core_image{};
        std::string_view path{};
        std::string_view title{};
        std::uint8_t ioctl_index = 0;
        char slot = 0;

        [[nodiscard]] bool names_a_game() const noexcept { return slot != 0 && !path.empty(); }
    };

    struct Record {
        std::string core;
        std::string core_image;
        std::string rbf;
        std::string path;
        std::string title;
        std::uint8_t ioctl_index = 0;
        char slot = 0;
        bool session = false;
    };

    static constexpr std::size_t kFiles = 32;
    static constexpr std::size_t kRecords = 64;

    void refresh(const svc::Vfs& files, RecentsStore& recents, const Played& session,
                 infra::OptRef<const os::IClock> clock = {},
                 std::span<const PlayedHistory::Record> played = {});

    void update(const svc::Vfs& files, RecentsStore& recents, const Played& session,
                infra::OptRef<const os::IClock> clock = {},
                std::span<const PlayedHistory::Record> played = {});

    [[nodiscard]] bool built() const noexcept { return built_; }
    [[nodiscard]] bool empty() const noexcept { return records_.empty(); }
    [[nodiscard]] std::span<const Record> records() const noexcept { return records_; }

    [[nodiscard]] std::uint32_t hidden() const noexcept { return hidden_; }

    [[nodiscard]] std::uint32_t files_read() const noexcept { return files_read_; }
    [[nodiscard]] std::uint32_t rereads() const noexcept { return rereads_; }

    [[nodiscard]] std::int64_t build_ns() const noexcept { return build_ns_; }
    [[nodiscard]] std::int64_t update_ns_max() const noexcept { return update_ns_max_; }

private:
    struct Source {
        std::string core;
        RecentsStore::ListId id{};
        std::int64_t mtime = 0;
        std::vector<RecentsStore::Entry> entries;
    };

    struct CoreName {
        std::string stem;
        std::string image;
    };

    void scan_cores_(const svc::Vfs& files);
    void read_sources_(const svc::Vfs& files, RecentsStore& recents);

    void reread_source_(const svc::Vfs& files, RecentsStore& recents, const Played& session);
    void assemble_(const Played& session, std::span<const PlayedHistory::Record> played);
    void sort_sources_();
    [[nodiscard]] const CoreName* core_named_(std::string_view stem) const noexcept;

    std::vector<CoreName> cores_;
    std::vector<Source> sources_;
    std::vector<Record> records_;
    std::uint32_t hidden_ = 0;
    std::uint32_t files_read_ = 0;
    std::uint32_t rereads_ = 0;
    std::int64_t build_ns_ = 0;
    std::int64_t update_ns_max_ = 0;
    bool built_ = false;
};

[[nodiscard]] std::string recent_core_name(std::string_view path_or_name);

[[nodiscard]] std::string recent_rbf_prefix(std::string_view core_image);

}  // namespace mister::app
