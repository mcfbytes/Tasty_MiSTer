// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/country_pick_plan.h"

#include <array>
#include <string>

#include "infra/message_sum.h"
#include "os/country_code.h"
#include "svc/connect_request.h"
#include "ui/connect_text.h"
#include "ui/hd_plan_inputs.h"
#include "ui/slot_range.h"

namespace mister::ui {
namespace {

using RowKind = app::PageDescription::RowKind;
using Request = svc::ConnectRequest;

struct Country {
    std::string_view code;
    std::string_view name;
};

constexpr std::array<Country, 40> kCountries{{
    {"US", "United States"},
    {"CA", "Canada"},
    {"MX", "Mexico"},
    {"BR", "Brazil"},
    {"AR", "Argentina"},
    {"CL", "Chile"},
    {"GB", "United Kingdom"},
    {"IE", "Ireland"},
    {"FR", "France"},
    {"DE", "Germany"},
    {"ES", "Spain"},
    {"PT", "Portugal"},
    {"IT", "Italy"},
    {"NL", "Netherlands"},
    {"BE", "Belgium"},
    {"LU", "Luxembourg"},
    {"CH", "Switzerland"},
    {"AT", "Austria"},
    {"DK", "Denmark"},
    {"SE", "Sweden"},
    {"NO", "Norway"},
    {"FI", "Finland"},
    {"PL", "Poland"},
    {"CZ", "Czechia"},
    {"HU", "Hungary"},
    {"GR", "Greece"},
    {"TR", "Turkey"},
    {"RU", "Russia"},
    {"UA", "Ukraine"},
    {"IL", "Israel"},
    {"AE", "United Arab Emirates"},
    {"ZA", "South Africa"},
    {"IN", "India"},
    {"CN", "China"},
    {"JP", "Japan"},
    {"KR", "South Korea"},
    {"TW", "Taiwan"},
    {"HK", "Hong Kong"},
    {"SG", "Singapore"},
    {"AU", "Australia"},
}};

constexpr SlotRange kTable = SlotRange::table(RowCount::of(kCountries.size()));

}

CountryPickPlan::CountryPickPlan(const HdPlanInputs& in) : ConnectSlotPlan(in, kTable.total()) {
    TASTY_SEAT_BODY(CountryPickPlan);
}

PageId CountryPickPlan::id() const {
    TASTY_SEAT_BODY(CountryPickPlan);
    return PageId::CountryPick;
}

std::string_view CountryPickPlan::title() const {
    TASTY_SEAT_BODY(CountryPickPlan);
    return "Wi-Fi country";
}

ConnectDemand CountryPickPlan::connect_demand() const {
    TASTY_SEAT_BODY(CountryPickPlan);
    return ConnectDemand::wifi_page();
}

bool CountryPickPlan::slot_shown(SlotId slot) const {
    TASTY_SEAT_BODY(CountryPickPlan);
    return kTable.contains(slot);
}

app::PageDescription::Row CountryPickPlan::slot_describe(SlotId slot) const {
    TASTY_SEAT_BODY(CountryPickPlan);
    const Country& c = kCountries[kTable.subscript_of(slot).value_or(0)];
    const std::optional<svc::WifiView>& v = views().wifi;
    const bool current = v && v->country && v->country->code() == c.code;
    std::string label{c.code};
    label.append("  ").append(c.name);
    return connect_row(RowKind::Value, label, current ? "current" : "");
}

Transition CountryPickPlan::slot_select(SlotId slot) {
    TASTY_SEAT_BODY(CountryPickPlan);
    const std::optional<std::size_t> at = kTable.subscript_of(slot);
    if (!at)
        return Transition{};
    if (const auto code = os::CountryCode::parse(kCountries[*at].code)) {
        (void)send(infra::make<Request>(Request::WifiSetCountry{*code}));
    }
    return Transition::pop();
}

}
