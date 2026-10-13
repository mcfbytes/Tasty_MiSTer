// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/slint_raster.h"

#include "infra/opt_ref.h"
#include "infra/persist.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "app/hd_crumb.h"
#include "app/hd_help.h"
#include "app/hd_metrics.h"
#include "app/hd_panel_plan.h"
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
    infra::OptRef<Adapter> adapter{};
    slint_plat::SoftwareRenderer::RepaintBufferType type =
        slint_plat::SoftwareRenderer::RepaintBufferType::ReusedBuffer;
    slint::PhysicalSize size{};

    std::unique_ptr<slint_plat::WindowAdapter> create_window_adapter() override {
        auto made = std::make_unique<Adapter>(type, size);
        adapter = infra::opt_ref_of(made.get());
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

[[nodiscard]] MenuRow menu_row(const PageDescription::Row& row) {
    MenuRow out;
    out.label = slint::SharedString{row.label.c_str()};
    out.value = slint::SharedString{row.value.c_str()};
    out.kind = static_cast<int>(row.kind);
    out.icon = static_cast<int>(row.icon);
    out.selected = row.selected;
    out.enabled = row.enabled;
    out.steps = row.steps;
    out.fill = static_cast<int>(row.fill);
    return out;
}

[[nodiscard]] slint::SharedString shared(std::string_view s) { return slint::SharedString{s}; }

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
    std::shared_ptr<slint::VectorModel<MenuRow>> pinned{};
    std::shared_ptr<slint::VectorModel<MenuRow>> choices{};
    std::shared_ptr<slint::VectorModel<HelpItem>> help{};
    int badge_kind = -1;
};

struct SlintRaster::Impl {
    HdTheme theme{};
    HdLayout layout{};
    HdMetrics metrics{};
    bool have_theme = false;
    bool force_full = false;
    infra::OptRef<Plat> plat{};
    std::unique_ptr<Scene> scene{};

    void drop_scene() {
        if (plat) plat->adapter.reset();
        scene.reset();
    }

    void push_theme() {
        if (!scene || !scene->ui || !have_theme) return;
        const auto px = [](std::int32_t v) { return static_cast<float>(v); };
        const HdMetrics& m = metrics;
        auto& tok = (*scene->ui)->global<Theme>();
        tok.set_primary(color_of(theme.primary));
        tok.set_accent(color_of(theme.accent));
        tok.set_text(color_of(theme.text));
        tok.set_bg_bar(color_of(theme.bg_bar));
        tok.set_card(color_of(theme.card));
        tok.set_border_subtle(color_of(theme.border_subtle));
        tok.set_border_mid(color_of(theme.border_mid));
        tok.set_text_label(color_of(theme.text_label));
        tok.set_selection_fill(color_of(theme.selection_fill));
        tok.set_on_accent(color_of(theme.on_accent));
        tok.set_cue_spent(color_of(theme.cue_spent));
        tok.set_control_edge(color_of(theme.control_edge));
        tok.set_family(slint::SharedString{"Noto Sans"});
        tok.set_weight_normal(400);
        tok.set_weight_selected(500);
        tok.set_weight_bold(700);
        tok.set_font_title(px(m.font_title));
        tok.set_font_body(px(m.font_body));
        tok.set_font_caption(px(m.font_caption));
        tok.set_inset_x(px(m.inset_x));
        tok.set_inset_y(px(m.inset_y));
        tok.set_pad_right(px(m.pad_right));
        tok.set_header_gap(px(m.header_gap));
        tok.set_list_top(px(m.list_top));
        tok.set_help_top(px(m.help_top));
        tok.set_help_pad(px(m.help_pad));
        tok.set_gap(px(m.gap));
        tok.set_row_h(px(m.row_h));
        tok.set_sep_h(px(m.row_h / 2));
        tok.set_row_pad(px(m.row_pad));
        tok.set_card_pad(px(m.card_pad));
        tok.set_radius_md(px(m.radius_md));
        tok.set_radius_sm(px(m.radius_sm));
        tok.set_stroke(px(m.stroke));
        tok.set_hair(1.f);
        tok.set_focus_stroke(px(m.focus_stroke));
        tok.set_ring_w(px(m.ring_w));
        tok.set_press_edge(px(m.press_edge));
        tok.set_glyph(px(m.glyph));
        tok.set_icon(px(m.icon));
        tok.set_button_h(px(m.button_h));
        tok.set_track_h(px(m.track_h));
    }

    void apply_size() {
        if (!scene || !scene->ui || layout.panel.w <= 0 || layout.panel.h <= 0) return;
        const auto pw = static_cast<std::uint32_t>(layout.panel.w);
        const auto ph = static_cast<std::uint32_t>(layout.panel.h);
        if (plat) plat->size = slint::PhysicalSize{{pw, ph}};
        if (plat && plat->adapter) plat->adapter->size_ = plat->size;
        auto& win = (*scene->ui)->window();
        win.dispatch_scale_factor_change_event(1.f);
        win.dispatch_resize_event(slint::LogicalSize{
            {static_cast<float>(layout.panel.w), static_cast<float>(layout.panel.h)}});
    }

    void adopt_layout(const HdLayout& next) {
        layout = next;
        const auto w = static_cast<std::uint32_t>(std::max(0, next.game.x + next.game.w));
        metrics = hd_metrics(w, static_cast<std::uint32_t>(std::max(0, next.panel.h)));
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
        plat = infra::opt_ref_of(shared_platform());
        if (!scene) {
            scene = std::make_unique<Scene>();
            scene->ui = HdMenu::create();
            (*scene->ui)->show();
            push_theme();
        }
        apply_size();
        return plat && plat->adapter && scene->ui.has_value();
    }

    template <class Row, class Bind>
    static void put(std::shared_ptr<slint::VectorModel<Row>>& model, std::vector<Row> next,
                    Bind bind) {
        if (!model || model->row_count() != next.size()) {
            model = std::make_shared<slint::VectorModel<Row>>(std::move(next));
            bind(model);
            return;
        }
        for (std::size_t i = 0; i < next.size(); ++i) {
            const auto cur = model->row_data(i);
            if (!cur || !(*cur == next[i])) model->set_row_data(i, next[i]);
        }
    }

    void sync(const PageDescription& page) {
        auto& ui = *scene->ui;
        const HdPanelPlan plan = plan_panel(metrics, page);
        const auto set_if = [](auto cur, auto next, auto apply) {
            if (cur != next) apply(next);
        };
        set_if(ui->get_heading_text(), slint::SharedString{page.title.c_str()},
               [&](const slint::SharedString& v) { ui->set_heading_text(v); });
        set_if(ui->get_core_text(), shared(hd_crumb_core(page.crumb.view())),
               [&](const slint::SharedString& v) { ui->set_core_text(v); });
        set_if(ui->get_subtitle_text(), shared(page.subtitle.view()),
               [&](const slint::SharedString& v) { ui->set_subtitle_text(v); });
        auto& tok = ui->global<Theme>();
        set_if(tok.get_row_h(), static_cast<float>(plan.row_h), [&](float v) { tok.set_row_h(v); });
        set_if(tok.get_sep_h(), static_cast<float>(plan.sep_h), [&](float v) { tok.set_sep_h(v); });
        const bool choice = page.shape == PageDescription::Shape::Choice;
        set_if(ui->get_choice_shape(), choice, [&](bool v) { ui->set_choice_shape(v); });
        set_if(ui->get_choice_stacked(), plan.choice_stacked,
               [&](bool v) { ui->set_choice_stacked(v); });
        set_if(ui->get_main_h(), static_cast<float>(plan.main_h),
               [&](float v) { ui->set_main_h(v); });
        set_if(ui->get_pinned_y(), static_cast<float>(plan.pinned_y),
               [&](float v) { ui->set_pinned_y(v); });
        set_if(ui->get_pinned_h(), static_cast<float>(plan.pinned_h),
               [&](float v) { ui->set_pinned_h(v); });
        set_if(ui->get_choice_y(), static_cast<float>(plan.choice_y),
               [&](float v) { ui->set_choice_y(v); });
        set_if(ui->get_choice_h(), static_cast<float>(plan.choice_h),
               [&](float v) { ui->set_choice_h(v); });
        set_if(ui->get_cue_shown(), plan.cue, [&](bool v) { ui->set_cue_shown(v); });
        set_if(ui->get_cue_up(), plan.cue_up, [&](bool v) { ui->set_cue_up(v); });
        set_if(ui->get_cue_down(), plan.cue_down, [&](bool v) { ui->set_cue_down(v); });
        char cue[24];
        std::snprintf(cue, sizeof(cue), "%u / %u", static_cast<unsigned>(plan.cue_at),
                      static_cast<unsigned>(plan.cue_total));
        set_if(ui->get_cue_text(), slint::SharedString{cue},
               [&](const slint::SharedString& v) { ui->set_cue_text(v); });

        std::vector<MenuRow> main;
        std::vector<MenuRow> pinned;
        std::vector<MenuRow> choices;
        for (std::uint8_t i = 0; i < plan.main_count; ++i)
            main.push_back(menu_row(page.rows[i]));
        for (std::uint8_t i = 0; i < plan.pinned_count; ++i)
            pinned.push_back(menu_row(page.rows[plan.pinned_first + i]));
        for (std::uint8_t i = 0; i < plan.choice_count; ++i)
            choices.push_back(menu_row(page.rows[plan.choice_first + i]));
        put<MenuRow>(scene->rows, std::move(main), [&](auto m) { ui->set_entries(m); });
        put<MenuRow>(scene->pinned, std::move(pinned), [&](auto m) { ui->set_pinned_entries(m); });
        put<MenuRow>(scene->choices, std::move(choices), [&](auto m) { ui->set_choices(m); });

        const HdHelp help = hd_help(page.footer.view(), plan.steps);
        std::vector<HelpItem> items;
        for (std::uint8_t i = 0; i < help.count; ++i) {
            HelpItem it;
            it.glyph = static_cast<int>(help.items[i].glyph);
            it.label = shared(help.items[i].label);
            items.push_back(it);
        }
        put<HelpItem>(scene->help, std::move(items), [&](auto m) { ui->set_help(m); });
    }

    void sync_badge(const HdBadgeView& badge) {
        if (!scene || !scene->ui) return;
        const int kind = static_cast<int>(badge.badge);
        if (scene->badge_kind == kind) return;
        (*scene->ui)->set_badge_kind(kind);
        scene->badge_kind = kind;
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
    impl_->adopt_layout(layout);
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
    impl_->adopt_layout(layout);
    impl_->drop_scene();
    impl_->force_full = true;
}

}  // namespace mister::app
