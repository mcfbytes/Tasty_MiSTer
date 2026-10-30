// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/slint_raster.h"

#include "infra/opt_ref.h"
#include "infra/persist.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/hd_badge_view.h"
#include "app/hd_crumb.h"
#include "app/hd_help.h"
#include "app/hd_metrics.h"
#include "app/hd_panel_plan.h"
#include "app/hd_theme.h"
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
        : renderer_(type),
          size_(size) {}

    slint_plat::AbstractRenderer& renderer() override { return renderer_; }

    slint::PhysicalSize size() override { return size_; }
};

struct Plat : slint_plat::Platform {
    infra::OptRef<Adapter> adapter{};
    slint_plat::SoftwareRenderer::RepaintBufferType
        type = slint_plat::SoftwareRenderer::RepaintBufferType::ReusedBuffer;
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
    return slint::Color::from_argb_uint8(
        255,
        static_cast<std::uint8_t>(rgb >> 16),
        static_cast<std::uint8_t>(rgb >> 8),
        static_cast<std::uint8_t>(rgb)
    );
}

[[nodiscard]] MenuRow menu_row(const PageDescription::Row& row, const HdTheme& theme) {
    MenuRow out;
    out.label = slint::SharedString{row.label.c_str()};
    out.value = slint::SharedString{row.value.c_str()};
    out.kind = static_cast<int>(row.kind);
    out.icon = static_cast<int>(row.icon);
    out.selected = row.selected;
    out.enabled = row.enabled;
    out.steps = row.steps;
    out.fill = static_cast<int>(row.fill);
    out.tier = static_cast<int>(row.tier);
    if (row.tier == RowTier::Shelf) {
        const HdTint tint = hd_core_tint(theme, row.value.view());
        out.tint = color_of(tint.fill);
        out.tint_ink = color_of(tint.ink);
    }
    return out;
}

[[nodiscard]] slint::SharedString shared(std::string_view s) {
    return slint::SharedString{s};
}

[[nodiscard]] MenuRow hub_row(const PageDescription::Hub& hub, std::size_t i) {
    const PageDescription::Hub::Item& item = hub.items[i];
    MenuRow out;
    out.label = slint::SharedString{item.label.c_str()};
    out.kind = static_cast<int>(PageDescription::RowKind::Action);
    out.icon = static_cast<int>(item.icon);
    out.selected = i == hub.open;
    out.enabled = item.enabled;
    out.tier = static_cast<int>(RowTier::Verb);
    return out;
}

[[nodiscard]] int rail_slot(char letter) noexcept {
    if (letter == '#')
        return 1;
    if (letter >= 'A' && letter <= 'Z')
        return 2 + (letter - 'A');
    return 0;
}

[[nodiscard]] HdDirty dirty_rects(
    const slint_plat::SoftwareRenderer::PhysicalRegion& region,
    std::int32_t panel_w,
    std::int32_t panel_h,
    HdDirty out = {}
) {
    constexpr std::size_t kPass = 3;
    static_assert(2 * kPass <= HdDirty::kMaxRects);
    const std::size_t end = std::min<std::size_t>(out.count + kPass, HdDirty::kMaxRects);
    for (auto piece : region.rectangles()) {
        const auto pw = static_cast<std::int32_t>(piece.size.width);
        const auto ph = static_cast<std::int32_t>(piece.size.height);
        const std::int32_t x0 = std::max(0, static_cast<std::int32_t>(piece.origin.x));
        const std::int32_t y0 = std::max(0, static_cast<std::int32_t>(piece.origin.y));
        const std::int32_t x1 = std::min(panel_w, piece.origin.x + pw);
        const std::int32_t y1 = std::min(panel_h, piece.origin.y + ph);
        if (pw <= 0 || ph <= 0 || x1 <= x0 || y1 <= y0)
            continue;
        if (out.count < end) {
            out.rects[out.count++] = HdRect{x0, y0, x1 - x0, y1 - y0};
            continue;
        }
        HdRect& last = out.rects[end - 1];
        const std::int32_t lx1 = std::max(last.x + last.w, x1);
        const std::int32_t ly1 = std::max(last.y + last.h, y1);
        last.x = std::min(last.x, x0);
        last.y = std::min(last.y, y0);
        last.w = lx1 - last.x;
        last.h = ly1 - last.y;
    }
    return out;
}

}

template <class C>
struct Scene {
    std::optional<slint::ComponentHandle<C>> ui{};
    std::shared_ptr<slint::VectorModel<MenuRow>> rows{};
    std::shared_ptr<slint::VectorModel<MenuRow>> pinned{};
    std::shared_ptr<slint::VectorModel<MenuRow>> choices{};
    std::shared_ptr<slint::VectorModel<MenuRow>> hub{};
    std::shared_ptr<slint::VectorModel<HelpItem>> help{};
    std::optional<HdOsdStatus::Badge> badge{};
    std::string status_text;
    bool drawn = false;
};

template <class C>
struct SlintRasterOf<C>::Impl {
    explicit Impl(const HdTemplateTraits& t) : traits(t) {}

    HdTemplateTraits traits;
    HdTheme theme{};
    HdLayout layout{};
    HdMetrics metrics{};
    HdMetrics card_metrics{};
    PageDescription fitted{};
    std::uint16_t fit_top = 0;
    decltype(PageDescription::title) fit_title{};
    std::uint16_t fit_total = 0;
    bool have_theme = false;
    bool force_full = false;
    infra::OptRef<Plat> plat{};
    std::unique_ptr<Scene<C>> scene{};

    void drop_scene() {

        if (scene && scene->ui)
            (*scene->ui)->hide();
        if (plat)
            plat->adapter.reset();
        scene.reset();
    }

    void push_theme() {
        if (!scene || !scene->ui || !have_theme)
            return;
        const auto px = [](std::int32_t v) {
            return static_cast<float>(v);
        };
        const HdMetrics& m = metrics;
        auto& tok = (*scene->ui)->template global<Theme>();
        tok.set_primary(color_of(theme.primary));
        tok.set_accent(color_of(theme.accent));
        tok.set_text(color_of(theme.text));
        tok.set_bg_bar(color_of(theme.bg_bar));
        tok.set_card(color_of(theme.card));
        const HdThemeGlass& glass = theme.glass;
        tok.set_ground(
            color_of(glass.on() ? glass.ground_key : hd_ground_fill(theme, traits.ground))
        );
        tok.set_plate(color_of(glass.on() ? glass.plate_key : theme.card));
        tok.set_band(color_of(glass.on() ? glass.ground_key : theme.bg_bar));
        tok.set_border_subtle(color_of(theme.border_subtle));
        tok.set_border_mid(color_of(theme.border_mid));
        tok.set_text_label(color_of(theme.text_label));
        tok.set_selection_fill(color_of(theme.selection_fill));
        tok.set_on_accent(color_of(theme.on_accent));
        tok.set_cue_spent(color_of(theme.cue_spent));
        tok.set_control_edge(color_of(theme.control_edge));
        tok.set_window(color_of(theme.window));
        tok.set_window_focus(color_of(theme.window_focus));
        tok.set_on_window_focus(color_of(theme.on_window_focus));
        tok.set_window_hand(color_of(theme.window_hand));
        tok.set_family(shared(traits.family));
        tok.set_subtitle_sep(shared(traits.subtitle_sep));
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
        tok.set_pixel(px(hd_art_pixel(m, traits.pixel_em)));
    }

    void apply_size() {
        const HdLayout::Rect box = hd_menu_box(layout);
        if (!scene || !scene->ui || box.w <= 0 || box.h <= 0)
            return;
        const auto pw = static_cast<std::uint32_t>(box.w);
        const auto ph = static_cast<std::uint32_t>(box.h);
        if (plat)
            plat->size = slint::PhysicalSize{{pw, ph}};
        if (plat && plat->adapter)
            plat->adapter->size_ = plat->size;
        auto& win = (*scene->ui)->window();
        win.dispatch_scale_factor_change_event(1.f);
        win.dispatch_resize_event(
            slint::LogicalSize{{static_cast<float>(box.w), static_cast<float>(box.h)}}
        );
    }

    void adopt_layout(const HdLayout& next) {
        layout = next;
        const auto w = static_cast<std::uint32_t>(std::max(0, next.surface.w));
        const auto h = static_cast<std::uint32_t>(std::max(0, hd_menu_box(next).h));
        card_metrics = hd_snap_type(hd_metrics(w, h), traits.pixel_em);
        metrics = hd_under_band(card_metrics, next.band.h);
    }

    const PageDescription& fit(const PageDescription& page) {
        if (!(page.title == fit_title) || page.total != fit_total) {
            fit_title = page.title;
            fit_total = page.total;
            fit_top = page.first;
        }
        const HdFit f = hd_fit_rows(metrics, page, plan_panel(card_metrics, page).row_h, fit_top);
        fit_top = f.top;
        fitted = f.page;
        return fitted;
    }

    [[nodiscard]] bool ensure() {
        const HdLayout::Rect box = hd_menu_box(layout);
        if (!have_theme || box.w <= 0 || box.h <= 0)
            return have_theme;
        if (shared_platform() == nullptr) {
            auto made = std::make_unique<Plat>();
            made->size = slint::PhysicalSize{
                {static_cast<std::uint32_t>(box.w), static_cast<std::uint32_t>(box.h)}
            };
            shared_platform() = made.get();
            slint_plat::set_platform(std::move(made));
        }
        plat = infra::opt_ref_of(shared_platform());
        if (!scene) {
            scene = std::make_unique<Scene<C>>();
            scene->ui = C::create();
            (*scene->ui)->show();
            push_theme();
        }
        apply_size();
        return plat && plat->adapter && scene->ui.has_value();
    }

    template <class Row, class Bind>
    static void put(
        std::shared_ptr<slint::VectorModel<Row>>& model,
        std::vector<Row> next,
        Bind bind
    ) {
        if (!model || model->row_count() != next.size()) {
            model = std::make_shared<slint::VectorModel<Row>>(std::move(next));
            bind(model);
            return;
        }
        for (std::size_t i = 0; i < next.size(); ++i) {
            const auto cur = model->row_data(i);
            if (!cur || !(*cur == next[i]))
                model->set_row_data(i, next[i]);
        }
    }

    void sync(const PageDescription& described) {
        const PageDescription& page = fit(described);
        auto& ui = *scene->ui;
        const HdPanelPlan plan = plan_panel(metrics, page);
        const auto set_if = [](auto cur, auto next, auto apply) {
            if (cur != next)
                apply(next);
        };
        set_if(
            ui->get_heading_text(),
            slint::SharedString{page.title.c_str()},
            [&](const slint::SharedString& v) { ui->set_heading_text(v); }
        );
        set_if(
            ui->get_core_text(),
            shared(hd_crumb_core(page.crumb.view())),
            [&](const slint::SharedString& v) { ui->set_core_text(v); }
        );
        set_if(
            ui->get_subtitle_text(),
            shared(page.subtitle.view()),
            [&](const slint::SharedString& v) { ui->set_subtitle_text(v); }
        );
        auto& tok = ui->template global<Theme>();
        set_if(tok.get_row_h(), static_cast<float>(plan.row_h), [&](float v) { tok.set_row_h(v); });
        set_if(tok.get_sep_h(), static_cast<float>(plan.sep_h), [&](float v) { tok.set_sep_h(v); });
        const bool choice = page.shape == PageDescription::Shape::Choice;
        set_if(ui->get_choice_shape(), choice, [&](bool v) { ui->set_choice_shape(v); });
        set_if(ui->get_choice_stacked(), plan.choice_stacked, [&](bool v) {
            ui->set_choice_stacked(v);
        });
        set_if(ui->get_main_h(), static_cast<float>(plan.main_h), [&](float v) {
            ui->set_main_h(v);
        });
        set_if(ui->get_pinned_y(), static_cast<float>(plan.pinned_y), [&](float v) {
            ui->set_pinned_y(v);
        });
        set_if(ui->get_pinned_h(), static_cast<float>(plan.pinned_h), [&](float v) {
            ui->set_pinned_h(v);
        });
        set_if(ui->get_choice_y(), static_cast<float>(plan.choice_y), [&](float v) {
            ui->set_choice_y(v);
        });
        set_if(ui->get_choice_h(), static_cast<float>(plan.choice_h), [&](float v) {
            ui->set_choice_h(v);
        });
        set_if(ui->get_across(), hd_runs_across(traits, page), [&](bool v) { ui->set_across(v); });
        set_if(ui->get_band_h(), static_cast<float>(layout.band.h), [&](float v) {
            ui->set_band_h(v);
        });
        set_if(ui->get_card_w(), static_cast<float>(layout.panel.w), [&](float v) {
            ui->set_card_w(v);
        });
        set_if(ui->get_band_focus(), page.hub.count != 0 && page.hub.depth == 0, [&](bool v) {
            ui->set_band_focus(v);
        });
        set_if(ui->get_list_first(), static_cast<int>(page.first), [&](int v) {
            ui->set_list_first(v);
        });
        set_if(ui->get_list_total(), static_cast<int>(page.total), [&](int v) {
            ui->set_list_total(v);
        });
        const int rail = rail_slot(page.letter);
        set_if(ui->get_rail_at(), rail, [&](int v) { ui->set_rail_at(v); });
        const char rail_char[2] = {rail == 0 ? '\0' : page.letter, '\0'};
        set_if(
            ui->get_rail_text(),
            slint::SharedString{rail_char},
            [&](const slint::SharedString& v) { ui->set_rail_text(v); }
        );
        set_if(ui->get_cue_shown(), plan.cue, [&](bool v) { ui->set_cue_shown(v); });
        set_if(ui->get_cue_up(), plan.cue_up, [&](bool v) { ui->set_cue_up(v); });
        set_if(ui->get_cue_down(), plan.cue_down, [&](bool v) { ui->set_cue_down(v); });
        char cue[24];
        std::snprintf(
            cue,
            sizeof(cue),
            "%u / %u",
            static_cast<unsigned>(plan.cue_at),
            static_cast<unsigned>(plan.cue_total)
        );
        set_if(ui->get_cue_text(), slint::SharedString{cue}, [&](const slint::SharedString& v) {
            ui->set_cue_text(v);
        });
        set_if(
            ui->get_caption_text(),
            shared(hd_now_playing(hd_crumb_core(page.crumb.view())).view()),
            [&](const slint::SharedString& v) { ui->set_caption_text(v); }
        );
        const HdScrollCounts counts = hd_scroll_counts(
            page.first,
            static_cast<std::uint32_t>(
                std::min<std::size_t>(page.row_count, PageDescription::kMaxRows)
            ),
            plan.cue_total
        );
        set_if(
            ui->get_above_text(),
            shared(counts.above.view()),
            [&](const slint::SharedString& v) { ui->set_above_text(v); }
        );
        set_if(
            ui->get_below_text(),
            shared(counts.below.view()),
            [&](const slint::SharedString& v) { ui->set_below_text(v); }
        );
        set_if(
            ui->get_above_count(),
            shared(counts.above_n.view()),
            [&](const slint::SharedString& v) { ui->set_above_count(v); }
        );
        set_if(
            ui->get_below_count(),
            shared(counts.below_n.view()),
            [&](const slint::SharedString& v) { ui->set_below_count(v); }
        );
        const auto rows = std::span{page.rows}.first(page.row_count);
        const bool shelf = std::ranges::any_of(rows, [](const PageDescription::Row& r) {
            return r.tier == RowTier::Shelf;
        });
        set_if(
            ui->get_shelf_caption_text(),
            shared(shelf ? kHdShelfCaption : std::string_view{}),
            [&](const slint::SharedString& v) { ui->set_shelf_caption_text(v); }
        );
        set_if(
            ui->get_strip_caption_text(),
            shared(kHdStripCaption),
            [&](const slint::SharedString& v) { ui->set_strip_caption_text(v); }
        );

        std::vector<MenuRow> main;
        std::vector<MenuRow> pinned;
        std::vector<MenuRow> choices;
        for (std::uint8_t i = 0; i < plan.main_count; ++i)
            main.push_back(menu_row(page.rows[i], theme));
        for (std::uint8_t i = 0; i < plan.pinned_count; ++i)
            pinned.push_back(menu_row(page.rows[plan.pinned_first + i], theme));
        for (std::uint8_t i = 0; i < plan.choice_count; ++i)
            choices.push_back(menu_row(page.rows[plan.choice_first + i], theme));
        put<MenuRow>(scene->rows, std::move(main), [&](auto m) { ui->set_entries(m); });
        put<MenuRow>(scene->pinned, std::move(pinned), [&](auto m) { ui->set_pinned_entries(m); });
        put<MenuRow>(scene->choices, std::move(choices), [&](auto m) { ui->set_choices(m); });

        const bool walks = layout.band.h > 0 && page.hub.depth == 0;
        const HdHelp help = hd_help(
            page.footer.view(),
            plan.steps  ? HdHelp::Lr::Change
                : walks ? HdHelp::Lr::Move
                        : HdHelp::Lr::None
        );
        std::vector<HelpItem> items;
        for (std::uint8_t i = 0; i < help.count; ++i) {
            HelpItem it;
            it.glyph = static_cast<int>(help.items[i].glyph);
            it.label = shared(help.items[i].label);
            items.push_back(it);
        }
        put<HelpItem>(scene->help, std::move(items), [&](auto m) { ui->set_help(m); });
    }

    void sync_band(const PageDescription& page) {
        auto& ui = *scene->ui;
        if (ui->get_hub_at() != static_cast<int>(page.hub.open))
            ui->set_hub_at(static_cast<int>(page.hub.open));
        const bool lit = page.hub.count != 0 && page.hub.depth == 0;
        if (ui->get_band_lit() != lit)
            ui->set_band_lit(lit);
        std::vector<MenuRow> hub;
        const std::size_t hub_n = std::min<std::size_t>(page.hub.count, page.hub.items.size());
        for (std::size_t i = 0; i < hub_n; ++i)
            hub.push_back(hub_row(page.hub, i));
        put<MenuRow>(scene->hub, std::move(hub), [&](auto m) { ui->set_hub_entries(m); });
    }

    void sync_badge(const HdBadgeView& badge) {
        if (!scene || !scene->ui)
            return;
        if (scene->badge != badge.badge) {
            (*scene->ui)->set_badge_text(shared(hd_badge_text(badge.badge)));
            (*scene->ui)->set_badge_state(static_cast<int>(badge.badge));
            scene->badge = badge.badge;
        }
        std::string text;
        if (badge.slots.bluetooth)
            text = "BT";
        if (badge.slots.rt)
            text += text.empty() ? "RT" : "  RT";
        if (scene->status_text != text) {
            (*scene->ui)->set_status_text(shared(text));
            scene->status_text = std::move(text);
        }
    }
};

template <class C>
SlintRasterOf<C>::SlintRasterOf(const HdTemplateTraits& traits)
    : impl_(std::make_unique<Impl>(traits)) {}

template <class C>
SlintRasterOf<C>::~SlintRasterOf() {
    if (!impl_ || !impl_->scene)
        return;
    if (current_seat() != SeatTag::HdOsd) {
        (void)impl_->scene.release();
        return;
    }
    impl_->drop_scene();
}

template <class C>
bool SlintRasterOf<C>::open_(const HdTheme& theme, const HdLayout& layout) {
    impl_->theme = theme;
    impl_->adopt_layout(layout);
    impl_->have_theme = true;
    impl_->push_theme();
    return impl_->ensure();
}

template <class C>
HdDirty SlintRasterOf<C>::render_(
    const PageDescription& page,
    const HdBadgeView& badge,
    std::span<std::uint16_t> panel,
    std::uint32_t stride_px
) {
    if (!impl_->ensure())
        return {};
    const HdLayout::Rect box = hd_menu_box(impl_->layout);
    const auto ph = box.h;
    const auto pw = box.w;
    const auto need = static_cast<std::size_t>(stride_px) * static_cast<std::size_t>(ph);
    if (pw <= 0 || ph <= 0 || stride_px == 0 || panel.size() < need)
        return {};
    auto* pixels = reinterpret_cast<slint_plat::Rgb565Pixel*>(panel.data());
    const auto draw = [&](HdDirty so_far) {
        slint_plat::update_timers_and_animations();
        const auto region = impl_->plat->adapter->renderer_.render(
            std::span<slint_plat::Rgb565Pixel>{pixels, panel.size()},
            stride_px
        );
        return dirty_rects(region, pw, ph, so_far);
    };

    impl_->sync_band(page);
    const bool band_pass = impl_->layout.band.h > 0 && impl_->scene->drawn;
    HdDirty out = band_pass ? draw({}) : HdDirty{};
    impl_->sync(page);
    impl_->sync_badge(badge);
    out = draw(out);
    impl_->scene->drawn = true;
    const bool full = impl_->force_full;
    impl_->force_full = false;
    if (full)
        return HdDirty{.rects = {HdRect{0, 0, pw, ph}}, .count = 1};
    return out;
}

template <class C>
void SlintRasterOf<C>::close_() {
    if (!impl_)
        return;
    impl_->drop_scene();
}

template <class C>
void SlintRasterOf<C>::relayout_(const HdLayout& layout) {
    impl_->adopt_layout(layout);
    impl_->drop_scene();
    impl_->force_full = true;
}

template class SlintRasterOf<HdTemplateZaparoo>;
template class SlintRasterOf<HdTemplateQuickBar>;
template class SlintRasterOf<HdTemplateGuide>;
template class SlintRasterOf<HdTemplatePixel>;

std::unique_ptr<HdRaster> make_hd_raster(HdTemplateId id) {
    switch (id) {
        case HdTemplateId::Zaparoo:
            return std::make_unique<SlintRasterOf<HdTemplateZaparoo>>(hd_template_traits(id));
        case HdTemplateId::QuickBar:
            return std::make_unique<SlintRasterOf<HdTemplateQuickBar>>(hd_template_traits(id));
        case HdTemplateId::Guide:
            return std::make_unique<SlintRasterOf<HdTemplateGuide>>(hd_template_traits(id));
        case HdTemplateId::Pixel:
            return std::make_unique<SlintRasterOf<HdTemplatePixel>>(hd_template_traits(id));
    }
    return nullptr;
}

HdRasters make_hd_rasters() {
    HdRasters out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = make_hd_raster(static_cast<HdTemplateId>(i));
    return out;
}

}
