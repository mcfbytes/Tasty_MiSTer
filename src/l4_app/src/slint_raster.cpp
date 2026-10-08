// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/slint_raster.h"

#include "infra/persist.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "infra/seat.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <slint-platform.h>
#include <slint.h>
#include "hd_menu.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mister::app {
namespace {

namespace slint_plat = slint::platform;

struct Adapter : slint_plat::WindowAdapter {
    slint_plat::SoftwareRenderer renderer_;
    slint::PhysicalSize size_;

    Adapter(slint_plat::SoftwareRenderer::RepaintBufferType type, slint::PhysicalSize size)
        : renderer_(type), size_(size) {}

    slint_plat::AbstractRenderer& renderer() override { return renderer_; }
    slint::PhysicalSize size() override { return size_; }
};

struct Plat : slint_plat::Platform {
    Adapter* adapter = nullptr;
    slint_plat::SoftwareRenderer::RepaintBufferType type =
        slint_plat::SoftwareRenderer::RepaintBufferType::ReusedBuffer;
    slint::PhysicalSize size{};

    std::unique_ptr<slint_plat::WindowAdapter> create_window_adapter() override {
        auto made = std::make_unique<Adapter>(type, size);
        adapter = made.get();
        return made;
    }
};

Plat*& shared_platform() {
    thread_local Plat* held TASTY_PERSIST(proc, hdosd_slint_platform){nullptr};
    return held;
}

[[nodiscard]] slint::Color color_of(std::uint32_t rgb) {
    return slint::Color::from_argb_uint8(255, static_cast<std::uint8_t>(rgb >> 16),
                                         static_cast<std::uint8_t>(rgb >> 8),
                                         static_cast<std::uint8_t>(rgb));
}

[[nodiscard]] float px_of(float fraction) noexcept { return fraction * 720.f; }

[[nodiscard]] MenuRow menu_row(const PageDescription::Row& row) {
    MenuRow out;
    out.label = slint::SharedString{row.label.c_str()};
    out.value = slint::SharedString{row.value.c_str()};
    out.kind = static_cast<int>(row.kind);
    out.selected = row.selected;
    out.enabled = row.enabled;
    return out;
}

[[nodiscard]] HdRect union_dirty(const slint_plat::SoftwareRenderer::PhysicalRegion& region,
                                 std::int32_t panel_w, std::int32_t panel_h) {
    bool any = false;
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    for (auto piece : region.rectangles()) {
        const auto pw = static_cast<std::int32_t>(piece.size.width);
        const auto ph = static_cast<std::int32_t>(piece.size.height);
        if (pw <= 0 || ph <= 0) continue;
        const std::int32_t rx0 = piece.origin.x;
        const std::int32_t ry0 = piece.origin.y;
        const std::int32_t rx1 = piece.origin.x + pw;
        const std::int32_t ry1 = piece.origin.y + ph;
        if (!any) {
            x0 = rx0;
            y0 = ry0;
            x1 = rx1;
            y1 = ry1;
            any = true;
        } else {
            x0 = std::min(x0, rx0);
            y0 = std::min(y0, ry0);
            x1 = std::max(x1, rx1);
            y1 = std::max(y1, ry1);
        }
    }
    if (!any) return {};
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > panel_w) x1 = panel_w;
    if (y1 > panel_h) y1 = panel_h;
    if (x1 <= x0 || y1 <= y0) return {};
    return HdRect{x0, y0, x1 - x0, y1 - y0};
}

}  // namespace

struct Scene {
    std::optional<slint::ComponentHandle<HdMenu>> ui{};
    std::shared_ptr<slint::VectorModel<MenuRow>> rows{};
    bool have_heading = false;
    slint::SharedString heading{};
    slint::SharedString crumb{};
    int badge_kind = -1;
    int badge_fps = -1;
};

struct SlintRaster::Impl {
    HdTheme theme{};
    HdLayout layout{};
    bool have_theme = false;
    bool force_full = false;
    Plat* plat = nullptr;
    std::unique_ptr<Scene> scene{};

    void drop_scene() {
        if (plat != nullptr) plat->adapter = nullptr;
        scene.reset();
    }

    void push_theme() {
        if (!scene || !scene->ui || !have_theme) return;
        auto& tok = (*scene->ui)->global<Theme>();
        tok.set_primary(color_of(theme.primary));
        tok.set_accent(color_of(theme.accent));
        tok.set_text(color_of(theme.text));
        tok.set_warning(color_of(theme.warning));
        tok.set_panel(color_of(theme.panel));
        tok.set_card(color_of(theme.card));
        tok.set_border(color_of(theme.border));
        tok.set_muted(color_of(theme.muted));
        tok.set_selection(color_of(theme.selection));
        tok.set_on_accent(color_of(theme.on_accent));
        tok.set_family(slint::SharedString{"Noto Sans"});
        tok.set_weight_medium(500);
        tok.set_weight_bold(700);
        tok.set_size_crumb(15.f);
        tok.set_size_heading(px_of(theme.title));
        tok.set_size_row(px_of(theme.row));
        tok.set_panel_pad(px_of(theme.pad));
        tok.set_gap(16.f);
        tok.set_row_height(44.f);
        tok.set_row_radius(6.f);
        tok.set_row_pad(14.f);
        tok.set_hair(1.f);
        tok.set_icon(16.f);
        tok.set_select_fade(static_cast<std::int64_t>(theme.fade_ms));
    }

    void apply_size() {
        if (!scene || !scene->ui || layout.panel.w <= 0 || layout.panel.h <= 0) return;
        const auto pw = static_cast<std::uint32_t>(layout.panel.w);
        const auto ph = static_cast<std::uint32_t>(layout.panel.h);
        if (plat != nullptr) plat->size = slint::PhysicalSize{{pw, ph}};
        if (plat != nullptr && plat->adapter != nullptr) plat->adapter->size_ = plat->size;
        const float scale = layout.scale > 0.f ? layout.scale : 1.f;
        auto& win = (*scene->ui)->window();
        win.dispatch_scale_factor_change_event(scale);
        win.dispatch_resize_event(slint::LogicalSize{{static_cast<float>(layout.panel.w) / scale,
                                                      static_cast<float>(layout.panel.h) / scale}});
    }

    [[nodiscard]] bool ensure() {
        if (!have_theme || layout.panel.w <= 0 || layout.panel.h <= 0) return have_theme;
        if (shared_platform() == nullptr) {
            auto made = std::make_unique<Plat>();
            made->size = slint::PhysicalSize{{static_cast<std::uint32_t>(layout.panel.w),
                                              static_cast<std::uint32_t>(layout.panel.h)}};
            shared_platform() = made.get();
            slint_plat::set_platform(std::move(made));
        }
        plat = shared_platform();
        if (!scene) {
            scene = std::make_unique<Scene>();
            scene->ui = HdMenu::create();
            (*scene->ui)->show();
            push_theme();
        }
        apply_size();
        return plat != nullptr && plat->adapter != nullptr && scene->ui.has_value();
    }

    void sync(const PageDescription& page) {
        const slint::SharedString next_heading{page.title.c_str()};
        const slint::SharedString next_crumb{page.crumb.c_str()};
        if (!scene->have_heading || scene->heading != next_heading) {
            (*scene->ui)->set_heading_text(next_heading);
            scene->heading = next_heading;
            scene->have_heading = true;
        }
        if (scene->crumb != next_crumb) {
            (*scene->ui)->set_crumb(next_crumb);
            scene->crumb = next_crumb;
        }
        const auto n =
            std::min(static_cast<std::size_t>(page.row_count), PageDescription::kMaxRows);
        std::vector<MenuRow> next;
        next.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
            next.push_back(menu_row(page.rows[i]));
        if (!scene->rows || scene->rows->row_count() != next.size()) {
            scene->rows = std::make_shared<slint::VectorModel<MenuRow>>(std::move(next));
            (*scene->ui)->set_entries(scene->rows);
            return;
        }
        for (std::size_t i = 0; i < next.size(); ++i) {
            const auto cur = scene->rows->row_data(i);
            if (!cur || !(*cur == next[i])) scene->rows->set_row_data(i, next[i]);
        }
    }

    void sync_badge(const HdBadgeView& badge) {
        if (!scene || !scene->ui) return;
        const int kind = static_cast<int>(badge.badge);
        const int fps = static_cast<int>(badge.fps);
        if (scene->badge_kind == kind && scene->badge_fps == fps) return;
        if (scene->badge_kind != kind) (*scene->ui)->set_badge_kind(kind);
        if (kind != static_cast<int>(HdOsdStatus::Badge::Unknown) &&
            (scene->badge_fps != fps || scene->badge_kind != kind)) {
            char line[40];
            std::snprintf(line, sizeof(line), "Live view \u00B7 %d fps", fps);
            (*scene->ui)->set_badge_live(slint::SharedString{line});
        }
        scene->badge_kind = kind;
        scene->badge_fps = fps;
    }
};

SlintRaster::SlintRaster() : impl_(std::make_unique<Impl>()) {}

SlintRaster::~SlintRaster() {
    if (!impl_ || !impl_->scene) return;
    if (current_seat() != SeatTag::HdOsd) {
        (void)impl_->scene.release();
        return;
    }
    impl_->drop_scene();
}

bool SlintRaster::open_(const HdTheme& theme, const HdLayout& layout) {
    impl_->theme = theme;
    impl_->layout = layout;
    impl_->have_theme = true;
    impl_->push_theme();
    return impl_->ensure();
}

HdRect SlintRaster::render_(const PageDescription& page, const HdBadgeView& badge,
                            std::span<std::uint16_t> panel, std::uint32_t stride_px) {
    if (!impl_->ensure()) return {};
    const auto ph = impl_->layout.panel.h;
    const auto pw = impl_->layout.panel.w;
    const auto need = static_cast<std::size_t>(stride_px) * static_cast<std::size_t>(ph);
    if (pw <= 0 || ph <= 0 || stride_px == 0 || panel.size() < need) return {};
    impl_->sync(page);
    impl_->sync_badge(badge);
    slint_plat::update_timers_and_animations();
    auto* pixels = reinterpret_cast<slint_plat::Rgb565Pixel*>(panel.data());
    const auto region = impl_->plat->adapter->renderer_.render(
        std::span<slint_plat::Rgb565Pixel>{pixels, panel.size()}, stride_px);
    const bool full = impl_->force_full;
    impl_->force_full = false;
    if (full) return HdRect{0, 0, pw, ph};
    return union_dirty(region, pw, ph);
}

void SlintRaster::close_() {
    if (!impl_) return;
    impl_->drop_scene();
}

void SlintRaster::relayout_(const HdLayout& layout) {
    impl_->layout = layout;
    impl_->drop_scene();
    impl_->force_full = true;
}

}  // namespace mister::app
