// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/progress_page.h"

#include <algorithm>
#include <array>

#include "ui/osd_renderer.h"

namespace mister::ui {

std::string_view progress_leaf(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    if (slash != std::string_view::npos)
        path.remove_prefix(slash + 1);
    return path.substr(0, std::min(path.size(), kProgressNameMax));
}

ProgressPage::ProgressPage(std::string_view name) noexcept {
    (void)name_.assign(progress_leaf(name));
}

void ProgressPage::set_progress(cores::Progress progress) noexcept {
    TASTY_SEAT_BODY(ProgressPage);
    last_ = progress;
    if (progress.dismisses())
        return;
    step_ = progress.scaled(kProgressMax);
}

bool ProgressPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ProgressPage);
    out = {};
    (void)out.title.assign("Loading");
    constexpr std::uint8_t kDescribedRows = 2;
    out.total = RowCount::of(kDescribedRows);
    out.row_count = RowCount::of(kDescribedRows);
    out.rows[0].kind = app::PageDescription::RowKind::Text;
    (void)out.rows[0].label.assign(name_.view());
    out.rows[1].kind = app::PageDescription::RowKind::Bar;
    if (!last_.dismisses()) {
        constexpr std::uint32_t kPercent = 100;

        out.rows[1].fill = static_cast<std::uint8_t>(last_.scaled(kPercent));
    }
    return true;
}

void ProgressPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ProgressPage);
    out.set_title("Loading");
    std::array<char, kProgressNameMax + 1> line{};
    line[0] = ' ';
    std::copy(name_.view().begin(), name_.view().end(), line.begin() + 1);
    for (RowCount r{}; r < kProgressOsdRows; r += kOneRow) {
        const OsdRow row = osd_row_at(r);
        if (row == kProgressNameRow) {
            out.text(row, std::string_view(line.data(), 1u + name_.size()));
        } else if (row == kProgressBarRow) {
            out.progress(row, step_);
        } else {
            out.text(row, std::string_view{});
        }
    }
}

std::unique_ptr<Page> make_progress_page(std::string_view name) {
    return std::make_unique<ProgressPage>(name);
}

}
