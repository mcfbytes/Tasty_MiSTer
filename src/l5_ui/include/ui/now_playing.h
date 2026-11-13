// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/path_text.h"
#include "app/ui_request_ring.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "ui/build_env.h"

namespace mister::cores {
struct FileSlot;
}

namespace mister::proto {
class ItemTable;
}

namespace mister::ui {

class NowPlaying final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Title = FixedStr<64, StrFit::Clip>;

    struct Played {
        app::PathText core_image{};

        std::optional<proto::FileRowId> row{};
        app::PathText path{};
        Title title{};
        std::int64_t unix_time = 0;
    };

    static constexpr std::size_t kFiles = 8;
    static constexpr std::string_view kNoGame = "No game loaded";

    bool on_done(
        infra::OptRef<const app::UiRequestRing::Pending> ask,
        std::span<const cores::FileSlot> slots,
        std::int64_t unix_time
    );

    void on_core_loaded(infra::OptRef<const app::UiRequestRing::Pending> load_core);

    [[nodiscard]] bool game_loaded() const noexcept { return game_; }

    [[nodiscard]] std::string_view title() const noexcept { return title_.view(); }

    [[nodiscard]] std::string_view path_of(proto::FileRowId row) const noexcept;

    [[nodiscard]] std::string_view core_image() const noexcept { return core_image_.view(); }

    [[nodiscard]] const Played& last() const noexcept { return last_; }

    [[nodiscard]] std::uint32_t unmatched() const noexcept { return unmatched_; }

    [[nodiscard]] std::string_view idle_line(const proto::ItemTable& items, const BuildEnv& env);

private:
    struct File {

        std::optional<proto::FileRowId> row{};
        app::PathText path{};
    };

    struct PrimaryRead {
        proto::BindGeneration gen{};
        proto::OsdMask mask{};
        bool read = false;
        bool has = false;
    };

    std::array<File, kFiles> files_{};
    std::size_t next_file_ = 0;
    app::PathText core_image_{};
    Title title_{};
    Played last_{};
    PrimaryRead primary_{};
    std::uint32_t unmatched_ = 0;
    bool game_ = false;
};

}
