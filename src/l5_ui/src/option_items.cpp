// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "ui/arc_label.h"
#include "ui/option_host.h"
#include "ui/option_items/addon_item.h"
#include "ui/option_items/cfg_ver_item.h"
#include "ui/option_items/cheats_item.h"
#include "ui/option_items/cycle_item.h"
#include "ui/option_items/def_mra_item.h"
#include "ui/option_items/dip_item.h"
#include "ui/option_items/exit_item.h"
#include "ui/option_items/file_item.h"
#include "ui/option_items/image_item.h"
#include "ui/option_items/info_item.h"
#include "ui/option_items/joy_names_item.h"
#include "ui/option_items/manual_item.h"
#include "ui/option_items/no_osd_item.h"
#include "ui/option_items/page_item.h"
#include "ui/option_items/separator_item.h"
#include "ui/option_items/toggle_close_item.h"
#include "ui/option_items/toggle_item.h"
#include "ui/option_items/unknown_item.h"
#include "ui/option_items/version_item.h"
#include "ui/options_page.h"
#include "ui/osd_renderer.h"
#include "ui/slot_labels.h"

namespace mister::ui {
namespace {

constexpr const char kStdExit[] = "            exit";
constexpr const char kStdBack[] = "            back";
constexpr const char kDipRow[] = " DIP Switches              \x16";
constexpr char kSubmenuArrow = '\x16';

const proto::Item& source_item(const OptionItem::Source& in) {
    return in.table->items()[in.item.v];
}

std::string_view field(const OptionItem::Source& in, unsigned n) {
    return in.table->subfield(source_item(in), n);
}

std::string ext_display(std::string_view ext) {
    std::string out;
    while (!ext.empty()) {
        std::string_view g = ext.substr(0, 3);
        while (!g.empty() && g.back() == ' ')
            g.remove_suffix(1);
        if (!out.empty())
            out.push_back(',');
        out.append(g);
        if (ext.size() <= 3)
            break;
        ext.remove_prefix(3);
    }
    return out;
}

class SlotLabels final : public ISlotLabels {
public:
    explicit SlotLabels(const std::vector<std::string>& v) noexcept : values_(&v) {}

    bool has_label(std::uint32_t v) const noexcept override {
        return v < values_->size() && !(*values_)[v].empty();
    }

private:
    const std::vector<std::string>* values_;
};

Transition refused(IOptionHost& host) {
    host.refuse();
    return Transition{};
}

app::PageDescription::Row described(app::PageDescription::RowKind kind, std::string_view label) {
    app::PageDescription::Row row{};
    row.kind = kind;
    (void)row.label.assign(label);
    return row;
}

[[noreturn]] void never_drawn(const char* what) {
    fatal(Error{Errc::negotiation, ERR_SITE(), 0}, what);
}

OptionItem::Carry plain_label(OptionItem& it, const OptionItem::Source& in) {
    it.label = std::string{field(in, 1)};
    return in.carry;
}

OptionItem::Carry build_slot(
    OptionItem& it,
    const OptionItem::Source& in,
    std::uint8_t default_index
) {
    it.label = std::string{field(in, 2)};
    it.ext_list = std::string{field(in, 1)};
    it.store_name = source_item(in).store_name;
    it.addon_ext = std::string{in.carry.addon};
    it.addon_after = in.carry.addon_after;
    const proto::Item& src = source_item(in);
    it.ioctl_index = src.has_digit ? src.digit : default_index;
    OptionItem::Carry next = in.carry;
    next.addon = {};
    next.addon_after = false;
    return next;
}

std::string compose_slot(const OptionItem& it, std::string_view verb) {
    const std::string ext = ext_display(it.ext_list);
    if (!it.label.empty())
        return " " + it.label + " *." + ext;
    return std::string{verb} + ext;
}

Transition activate_slot(const OptionItem& it, const OptionItem::Press& press, IOptionHost& host) {
    if (!press.select)
        return refused(host);
    return host.open_browser(it);
}

bool pulse_one_bit(const OptionItem& it, const OptionItem::Press& press, IOptionHost& host) {
    if (!press.select)
        return false;
    if (it.bits.width != 1) {
        host.refuse();
        return false;
    }
    host.toggle(it.bits, it.ext_word);
    return true;
}

}

std::unique_ptr<OptionItem> OptionItem::make(Kind k) {
    switch (k) {
        case Kind::Page:
            return std::make_unique<PageItem>();
        case Kind::File:
            return std::make_unique<FileItem>();
        case Kind::Image:
            return std::make_unique<ImageItem>();
        case Kind::Cheats:
            return std::make_unique<CheatsItem>();
        case Kind::Manual:
            return std::make_unique<ManualItem>();
        case Kind::Dip:
            return std::make_unique<DipItem>();
        case Kind::Toggle:
            return std::make_unique<ToggleItem>();
        case Kind::ToggleClose:
            return std::make_unique<ToggleCloseItem>();
        case Kind::Option:
            return std::make_unique<CycleItem>();
        case Kind::Separator:
            return std::make_unique<SeparatorItem>();
        case Kind::Addon:
            return std::make_unique<AddonItem>();
        case Kind::DefMra:
            return std::make_unique<DefMraItem>();
        case Kind::JoyNames:
            return std::make_unique<JoyNamesItem>();
        case Kind::Info:
            return std::make_unique<InfoItem>();
        case Kind::Version:
            return std::make_unique<VersionItem>();
        case Kind::CfgVer:
            return std::make_unique<CfgVerItem>();
        case Kind::NoOsd:
            return std::make_unique<NoOsdItem>();
        case Kind::Unknown:
        case Kind::kCount:
            return std::make_unique<UnknownItem>();
        case Kind::Exit:
            return std::make_unique<ExitItem>();
    }
    return std::make_unique<UnknownItem>();
}

OptionItem::Carry OptionItem::build(const Source& in, const BuildEnv& env) {
    TASTY_SEAT_BODY(OptionItem);
    return on_build(in, env);
}

std::string OptionItem::compose(const Look& look) const {
    TASTY_SEAT_BODY(OptionItem);
    return on_compose(look);
}

app::PageDescription::Row OptionItem::describe(const Look& look) const {
    TASTY_SEAT_BODY(OptionItem);
    app::PageDescription::Row row = on_describe(look);
    row.enabled = !disabled;
    row.selected = false;
    return row;
}

Transition OptionItem::activate(const Press& press, IOptionHost& host) const {
    TASTY_SEAT_BODY(OptionItem);
    return on_activate(press, host);
}

OptionItem::Carry PageItem::on_build(const Source& in, const BuildEnv&) {

    const std::uint8_t n = source_item(in).declared_page;
    target_page = (n >= 1 && n <= 9) ? n : std::uint8_t{0};
    return plain_label(*this, in);
}

std::string PageItem::on_compose(const Look& look) const {
    std::string s;
    if (look.flat) {

        s = " ";
        s.push_back(kSubmenuArrow);
        s += " ";
        s += label;
        s.resize(28, ' ');
    } else {

        s = " " + label;
        s.resize(27, ' ');
        s.push_back(kSubmenuArrow);
    }
    return s;
}

app::PageDescription::Row PageItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Submenu, label);
}

Transition PageItem::on_activate(const Press& press, IOptionHost& host) const {
    if (!press.select)
        return Transition{};
    if (target_page == 0)
        return refused(host);

    return Transition{Transition::Kind::Push, PageId::CoreOptions, ItemId{target_page}};
}

OptionItem::Carry FileItem::on_build(const Source& in, const BuildEnv&) {
    return build_slot(*this, in, static_cast<std::uint8_t>(in.taken.v + 1u));
}

std::string FileItem::on_compose(const Look&) const {
    return compose_slot(*this, " Load *.");
}

app::PageDescription::Row FileItem::on_describe(const Look&) const {
    const std::string text = compose_slot(*this, " Load *.");
    return described(app::PageDescription::RowKind::Action, std::string_view{text}.substr(1));
}

Transition FileItem::on_activate(const Press& press, IOptionHost& host) const {
    return activate_slot(*this, press, host);
}

OptionItem::Carry ImageItem::on_build(const Source& in, const BuildEnv&) {
    return build_slot(*this, in, 0u);
}

std::string ImageItem::on_compose(const Look&) const {
    return compose_slot(*this, " Mount *.");
}

app::PageDescription::Row ImageItem::on_describe(const Look&) const {
    const std::string text = compose_slot(*this, " Mount *.");
    return described(app::PageDescription::RowKind::Action, std::string_view{text}.substr(1));
}

Transition ImageItem::on_activate(const Press& press, IOptionHost& host) const {
    return activate_slot(*this, press, host);
}

OptionItem::Carry CheatsItem::on_build(const Source& in, const BuildEnv& env) {
    disabled = disabled || !env.cheats_available;
    return plain_label(*this, in);
}

std::string CheatsItem::on_compose(const Look&) const {
    return label.empty() ? std::string{" Cheats"} : " " + label;
}

app::PageDescription::Row CheatsItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Submenu, label);
}

Transition CheatsItem::on_activate(const Press& press, IOptionHost& host) const {
    if (!press.select)
        return refused(host);
    return Transition{Transition::Kind::Push, PageId::Cheats};
}

OptionItem::Carry ManualItem::on_build(const Source& in, const BuildEnv&) {
    label = " Manual";
    return in.carry;
}

std::string ManualItem::on_compose(const Look&) const {
    return label;
}

app::PageDescription::Row ManualItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Action, label);
}

Transition ManualItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry DipItem::on_build(const Source& in, const BuildEnv&) {
    label = kDipRow;
    return in.carry;
}

std::string DipItem::on_compose(const Look&) const {
    return label;
}

app::PageDescription::Row DipItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Submenu, label);
}

Transition DipItem::on_activate(const Press& press, IOptionHost& host) const {
    if (!press.select)
        return refused(host);
    return Transition{Transition::Kind::Push, PageId::ArcadeDip};
}

OptionItem::Carry ToggleItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string ToggleItem::on_compose(const Look&) const {
    return " " + label;
}

app::PageDescription::Row ToggleItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Action, label);
}

Transition ToggleItem::on_activate(const Press& press, IOptionHost& host) const {
    (void)pulse_one_bit(*this, press, host);
    return Transition{};
}

OptionItem::Carry ToggleCloseItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string ToggleCloseItem::on_compose(const Look&) const {
    return " " + label;
}

app::PageDescription::Row ToggleCloseItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Action, label);
}

Transition ToggleCloseItem::on_activate(const Press& press, IOptionHost& host) const {
    if (!pulse_one_bit(*this, press, host))
        return Transition{};
    return Transition{Transition::Kind::CloseAll};
}

OptionItem::Carry CycleItem::on_build(const Source& in, const BuildEnv& env) {
    for (unsigned n = 2; n < source_item(in).subfield_count; ++n) {
        values.push_back(resolve_arc(field(in, n), env.custom_aspect_ratio));
    }
    return plain_label(*this, in);
}

std::string_view CycleItem::shown_value(const Look& look) const {
    std::uint32_t x = look.value;
    if (x >= values.size() || values[x].empty())
        x = 0;
    return (x < values.size()) ? std::string_view{values[x]} : std::string_view{};
}

std::string CycleItem::on_compose(const Look& look) const {
    char row[kOptionRowWidth + 1] = {};
    format_key_value(row, label, shown_value(look));
    return std::string{row};
}

app::PageDescription::Row CycleItem::on_describe(const Look& look) const {
    app::PageDescription::Row row = described(app::PageDescription::RowKind::Value, label);
    row.steps = true;
    (void)row.value.assign(shown_value(look));
    return row;
}

Transition CycleItem::on_activate(const Press& press, IOptionHost& host) const {
    if (!bits.valid())
        return refused(host);
    const SlotLabels labels{values};
    const std::uint32_t next = cycle_value(bits, press.value, press.minus ? -1 : +1, &labels);
    host.write_status(bits, next & 0x00FF'FFFFu);
    return Transition{};
}

OptionItem::Carry SeparatorItem::on_build(const Source& in, const BuildEnv&) {
    if (in.table != nullptr)
        label = std::string{field(in, 1)};
    Carry next = in.carry;
    next.last_space = label.empty();
    return next;
}

std::string SeparatorItem::on_compose(const Look&) const {
    return " " + label;
}

app::PageDescription::Row SeparatorItem::on_describe(const Look&) const {
    return described(app::PageDescription::RowKind::Separator, {});
}

Transition SeparatorItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry AddonItem::on_build(const Source& in, const BuildEnv&) {
    const std::string_view head = field(in, 0);
    Carry next = in.carry;
    next.addon = field(in, 1);
    next.addon_after = (head.size() >= 2 && head[1] == '1');
    ext_list = std::string{next.addon};
    addon_after = next.addon_after;
    return next;
}

std::string AddonItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row AddonItem::on_describe(const Look&) const {
    never_drawn("AddonItem::on_describe");
}

Transition AddonItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry DefMraItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string DefMraItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row DefMraItem::on_describe(const Look&) const {
    never_drawn("DefMraItem::on_describe");
}

Transition DefMraItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry JoyNamesItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string JoyNamesItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row JoyNamesItem::on_describe(const Look&) const {
    never_drawn("JoyNamesItem::on_describe");
}

Transition JoyNamesItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry InfoItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string InfoItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row InfoItem::on_describe(const Look&) const {
    never_drawn("InfoItem::on_describe");
}

Transition InfoItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry VersionItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string VersionItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row VersionItem::on_describe(const Look&) const {
    never_drawn("VersionItem::on_describe");
}

Transition VersionItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry CfgVerItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string CfgVerItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row CfgVerItem::on_describe(const Look&) const {
    never_drawn("CfgVerItem::on_describe");
}

Transition CfgVerItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry NoOsdItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string NoOsdItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row NoOsdItem::on_describe(const Look&) const {
    never_drawn("NoOsdItem::on_describe");
}

Transition NoOsdItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry UnknownItem::on_build(const Source& in, const BuildEnv&) {
    return plain_label(*this, in);
}

std::string UnknownItem::on_compose(const Look&) const {
    return {};
}

app::PageDescription::Row UnknownItem::on_describe(const Look&) const {
    never_drawn("UnknownItem::on_describe");
}

Transition UnknownItem::on_activate(const Press&, IOptionHost& host) const {
    return refused(host);
}

OptionItem::Carry ExitItem::on_build(const Source& in, const BuildEnv& env) {
    label = (env.page == 0) ? kStdExit : kStdBack;
    return in.carry;
}

std::string ExitItem::on_compose(const Look&) const {
    return label;
}

app::PageDescription::Row ExitItem::on_describe(const Look&) const {
    const std::string_view text{label};
    return described(
        app::PageDescription::RowKind::Action,
        text.substr(std::min(text.find_first_not_of(' '), text.size()))
    );
}

Transition ExitItem::on_activate(const Press& press, IOptionHost&) const {
    return press.at_root ? Transition{Transition::Kind::Leave} : Transition{Transition::Kind::Pop};
}

}
