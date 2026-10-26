// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/placer.h"

#include <array>
#include <cstddef>
#include <memory>

#include "app/session_identity.h"
#include "ui/option_host.h"
#include "ui/options_model.h"
#include "ui/options_page.h"

namespace mister::ui {
namespace {

using Kind = OptionItem::Kind;

struct KindPlace {
    Kind kind;
    Placement base;
    bool slot;
    bool pulse;
};

constexpr std::array<KindPlace, kKindRowCount> kKindPlaces{{
    {Kind::Page, Placement::CoreSettings, false, false},
    {Kind::File, Placement::CoreSettings, true, false},
    {Kind::Image, Placement::CoreSettings, true, false},
    {Kind::Cheats, Placement::Cheats, false, false},
    {Kind::Manual, Placement::CoreSettings, false, false},
    {Kind::Dip, Placement::CoreSettings, false, false},
    {Kind::Toggle, Placement::CoreSettings, false, true},
    {Kind::ToggleClose, Placement::CoreSettings, false, true},
    {Kind::Option, Placement::CoreSettings, false, false},
    {Kind::Separator, Placement::CoreSettings, false, false},
    {Kind::Addon, Placement::Hidden, false, false},
    {Kind::DefMra, Placement::Hidden, false, false},
    {Kind::JoyNames, Placement::Hidden, false, false},
    {Kind::Info, Placement::CoreAbout, false, false},
    {Kind::Version, Placement::CoreAbout, false, false},
    {Kind::CfgVer, Placement::CoreAbout, false, false},
    {Kind::NoOsd, Placement::Hidden, false, false},
    {Kind::Unknown, Placement::Hidden, false, false},
    {Kind::Exit, Placement::Hidden, false, false},
}};

consteval bool kind_places_are_indexed_by_kind() {
    for (std::size_t i = 0; i < kKindPlaces.size(); ++i) {
        if (static_cast<std::size_t>(kKindPlaces[i].kind) != i)
            return false;
    }
    return true;
}

static_assert(kind_places_are_indexed_by_kind());

class PulseProbe final : public IOptionHost {
public:
    void refuse() override {}

    void write_status(proto::BitField, std::uint32_t) override {}

    void toggle(proto::BitField bits, bool ex) override {
        pulsed = true;
        bit = bits.start;
        ext = ex;
    }

    Transition open_browser(const OptionItem&) override { return Transition{}; }

    bool pulsed = false;
    proto::StatusBit bit{};
    bool ext = false;
};

}

Placer::Placer(
    const OptionsModel& walk,
    std::span<const cores::FileSlot> slots,
    const CoreVerbs& verbs,
    PlaceFacts facts
)
    : walk_(walk),
      game_(walk, slots) {
    TASTY_SEAT_BODY(Placer);
    placed_.reserve(walk_.items().size());
    bool reset_taken = false;
    for (const std::unique_ptr<OptionItem>& it : walk_.items()) {
        Placement p = classify_(*it, verbs, facts);

        if (p == Placement::ResetGame) {
            if (reset_taken)
                p = Placement::CoreSettings;
            reset_taken = true;
        }
        placed_.push_back(p);
        if (p == Placement::Slot && it->selectable_index && !it->disabled)
            slots_.push_back(it.get());
    }
}

Placement Placer::of(const OptionItem& item) const {
    TASTY_SEAT_BODY(Placer);
    const auto items = walk_.items();
    for (std::size_t i = 0; i < items.size() && i < placed_.size(); ++i) {
        if (items[i].get() == &item)
            return placed_[i];
    }
    return Placement::Hidden;
}

bool Placer::resets_the_game_(const OptionItem& it) const {
    if (it.page != 0 || !it.screen_row || !it.selectable_index || it.disabled)
        return false;
    PulseProbe probe;
    (void)it.activate(OptionItem::Press{.select = true, .at_root = true}, probe);
    return probe.pulsed && !probe.ext && probe.bit == proto::StatusBit{0};
}

Placement Placer::classify_(const OptionItem& it, const CoreVerbs& verbs, PlaceFacts facts) const {
    const KindPlace& row = kKindPlaces[static_cast<std::size_t>(it.kind)];
    if (row.slot) {
        return it.page == 0 && it.screen_row ? Placement::Slot : Placement::CoreSettings;
    }
    if (!row.pulse)
        return row.base;
    if (facts.savestate_declared && facts.savestate_live) {
        if (verbs.save && it.bits == *verbs.save)
            return Placement::SaveState;
        if (verbs.restore && it.bits == *verbs.restore)
            return Placement::LoadState;
    }
    return resets_the_game_(it) ? Placement::ResetGame : Placement::CoreSettings;
}

}
