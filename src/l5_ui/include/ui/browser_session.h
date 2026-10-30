// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/recents_store.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "ui/browser_entry.h"
#include "ui/list_view.h"

namespace mister::app {
class IRomsetAliases;
}

namespace mister::ui {

class IBrowseSource;
class IBrowserHost;

struct ScanOptions {
    bool include_dirs = false;
    bool cores_only = false;

    bool romset_browse = false;
    bool no_enter = false;
    bool no_zip = false;
};

struct BrowserPolicy {
    bool eject_on_backspace = false;
    bool clear_on_backspace = false;
    bool text_defaults = false;
    bool saves_home = false;
};

class BrowserSession {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Request {
        std::string start_path;
        std::string extensions;
        ScanOptions scan{};
        BrowserPolicy policy{};

        std::string games_dir;
        std::string home_path;

        bool cd_descend = false;

        std::optional<app::RecentsStore::ListId> recents{};
        std::string recents_core;
        bool recents_first = false;

        bool recent_entry = false;
        infra::OptRef<IBrowserHost> host{};

        infra::OptRef<app::IRomsetAliases> aliases{};
    };

    static constexpr std::size_t kPathMax = infra::kPathMax;
    static constexpr std::size_t kPathBudget = kPathMax - 100;
    static constexpr std::size_t kFilterMax = 255;
    static constexpr std::size_t kRememberMax = 127;
    static constexpr std::int64_t kFilterTimeoutNs = 2'000'000'000;

    [[nodiscard]] Ex<void> open(const IBrowseSource& src, Request req);

    [[nodiscard]] Ex<void> rescan();
    [[nodiscard]] Ex<void> change_dir(std::string_view name);

    void type_ahead(char c, std::int64_t now_ns);

    void note_navigation() noexcept { filter_deadline_ns_ = 0; }

    void clear_filter() noexcept { filter_.clear(); }

    std::string_view filter() const noexcept { return filter_; }

    void next_group();
    void prev_group();

    [[nodiscard]] char selected_group() const noexcept;

    void enter_recents(std::vector<BrowserEntry> rows);
    [[nodiscard]] Ex<void> exit_recents();

    bool recents_active() const noexcept { return recents_mode_; }

    const std::vector<BrowserEntry>& entries() const noexcept { return entries_; }

    ListView<BrowserEntry>& view() noexcept { return view_; }

    const ListView<BrowserEntry>& view() const noexcept { return view_; }

    const std::string& path() const noexcept { return path_; }

    std::string_view extensions() const noexcept { return ext_; }

    const Request& request() const noexcept { return req_; }

    std::string selected_full_path() const;

    std::uint32_t scans() const noexcept { return scans_; }

    std::uint32_t refusals() const noexcept { return refusals_; }

private:
    static std::string_view entry_name(const BrowserEntry& e) {
        TASTY_SEAT_BODY(BrowserSession);
        return e.altname;
    }

    [[nodiscard]] Ex<void> scan_list(std::string_view type_filter);
    void position_on(std::string_view raw_name);
    void select_dir_named(std::string_view name);
    void seek_letter(char c);
    void assign_display(BrowserEntry& e) const;

    [[nodiscard]] bool assign_romset_display(BrowserEntry& e) const;
    std::string resolve_content_home() const;

    infra::OptRef<const IBrowseSource> src_{};
    Request req_{};
    std::string ext_;
    std::vector<BrowserEntry> entries_;
    std::string filter_;
    std::string path_;
    std::string home_;
    bool recents_mode_ = false;
    std::int64_t filter_deadline_ns_ = 0;
    std::uint32_t scans_ = 0;
    std::uint32_t refusals_ = 0;
    ListView<BrowserEntry> view_{{}, &BrowserSession::entry_name, {}};
};

}
