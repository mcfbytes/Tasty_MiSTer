// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/mount_refusal_page.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "infra/byte_count.h"
#include "infra/indexed_array.h"
#include "infra/span_cut.h"
#include "ui/osd_renderer.h"
#include "ui/ui_event.h"

namespace mister::ui {
namespace {

constexpr RowCount kRefusalOsdRows{8};

constexpr infra::ByteCount kRowBytes{28};

}

PageFacts MountRefusalPage::facts() const {
    TASTY_SEAT_BODY(MountRefusalPage);
    PageFacts f{};
    f.osd_rows = kRefusalOsdRows;
    f.auto_pop = svc::kMountRefusalShows;
    f.keys = PageKeys::Below;
    f.allows_popups = true;
    return f;
}

namespace {

struct Lines {
    using Rows = infra::IndexedArray<std::string_view, 7, RowIndex>;
    Rows rows{};
    RowCount count{};
};

[[nodiscard]] Lines lines_of(std::string_view rest) noexcept {
    Lines out{};
    for (const RowIndex at : rows(RowCount::truncating(Lines::Rows::size()))) {
        if (rest.empty())
            break;
        const std::string_view head = infra::affine::first_clamped(rest, kRowBytes);
        const std::size_t cut = std::min(head.find('\n'), head.size());
        out.rows[at] = rest.substr(0, cut);
        rest.remove_prefix(std::min(rest.size(), cut + 1));
        out.count += kOneRow;
    }
    return out;
}

}

void MountRefusalPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(MountRefusalPage);
    out.set_title(svc::kMountRefusalTitle);
    const Lines l = lines_of(svc::mount_refusal_text(reason_));
    RowIndex at{};
    for (RowCount r{}; r < kRefusalOsdRows; r += kOneRow) {
        const OsdRow row = osd_row_at(r);
        if (row == OsdRow{} || !l.count.contains(at)) {
            out.text(row, std::string_view{});
            continue;
        }
        out.text(row, l.rows[at]);
        at = at.next();
    }
}

bool MountRefusalPage::describe(app::hd::PageDescription& out) const {
    TASTY_SEAT_BODY(MountRefusalPage);
    out = {};
    (void)out.title.assign(svc::kMountRefusalTitle);
    const Lines l = lines_of(svc::mount_refusal_text(reason_));
    for (const RowIndex i : rows(l.count)) {
        app::hd::PageDescription::Row& row = infra::at_index(out.rows, i);
        row.kind = app::hd::PageDescription::RowKind::Text;
        (void)row.label.assign(l.rows[i]);
    }
    out.row_count = l.count;
    out.total = out.row_count;
    return true;
}

Transition MountRefusalPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(MountRefusalPage);
    if (ev.buttons != UiButtonMask{})
        return Transition::pop();
    return Transition{};
}

std::unique_ptr<Page> make_mount_refusal_page(svc::MountRefusal reason) {
    return std::make_unique<MountRefusalPage>(reason);
}

}
