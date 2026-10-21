// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/progress_page.h"

#include <algorithm>
#include <cstring>

#include "ui/osd_renderer.h"

namespace mister::ui {

std::string_view progress_leaf(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    if (slash != std::string_view::npos)
        path.remove_prefix(slash + 1);
    return path.substr(0, std::min(path.size(), kProgressNameMax));
}

ProgressPage::ProgressPage(std::string_view name) noexcept {
    const std::string_view leaf = progress_leaf(name);
    std::copy(leaf.begin(), leaf.end(), name_);
    name_len_ = static_cast<std::uint8_t>(leaf.size());
}

void ProgressPage::set_progress(std::uint16_t cur, std::uint16_t max) noexcept {
    TASTY_SEAT_BODY(ProgressPage);
    cur_ = cur;
    max_ = max;
    if (max == 0)
        return;
    const std::uint64_t s = static_cast<std::uint64_t>(cur) * kProgressMax / max;
    step_ = static_cast<std::uint8_t>(std::min<std::uint64_t>(s, kProgressMax));
}

bool ProgressPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ProgressPage);
    out = {};
    (void)out.title.assign("Loading");
    out.total = 2;
    out.row_count = 2;
    out.rows[0].kind = app::PageDescription::RowKind::Text;
    (void)out.rows[0].label.assign(std::string_view{name_, name_len_});
    out.rows[1].kind = app::PageDescription::RowKind::Bar;
    if (max_ != 0) {
        const std::uint32_t pct = static_cast<std::uint32_t>(cur_) * 100u / max_;
        out.rows[1].fill = static_cast<std::uint8_t>(std::min<std::uint32_t>(100u, pct));
    }
    return true;
}

void ProgressPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ProgressPage);
    out.set_title("Loading");
    char line[kProgressNameMax + 1];
    line[0] = ' ';
    std::memcpy(line + 1, name_, name_len_);
    for (unsigned r = 0; r < 8; ++r) {
        const OsdRow row{static_cast<std::uint8_t>(r)};
        if (r == kProgressNameRow) {
            out.text(row, std::string_view(line, 1u + name_len_));
        } else if (r == kProgressBarRow) {
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
