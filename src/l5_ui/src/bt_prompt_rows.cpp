// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/bt_prompt_rows.h"

#include <utility>

#include "infra/decimal_text.h"
#include "infra/message_sum.h"
#include "os/passkey_digits.h"

namespace mister::ui {
namespace {

using Prompt = svc::BtPrompt;

constexpr char kOf = '/';

struct Projector {
    BtPromptRows out{};

    void on(const Prompt::ShowPasskey& p) {
        out.headline = "Type ";
        out.headline.append(p.passkey.format6().view()).append(" on the controller, then Enter");
        out.value = std::string{p.digits.text().view()};
        out.value.push_back(kOf);
        out.value.append(infra::DecimalText{os::PasskeyDigits::kMax}.view());
        out.gen = p.gen;
    }

    void on(const Prompt::ShowPin& p) {
        out.headline = "Type PIN ";
        out.headline.append(p.pin.view()).append(" on the controller");
        out.gen = p.gen;
    }

    void on(const Prompt::Confirm& p) {
        out.headline = "Is the number ";
        out.headline.append(p.passkey.format6().view()).append(" shown on the controller?");
        out.ask = BtPromptRows::Ask::YesNo;
        out.gen = p.gen;
    }

    void on(const Prompt::EnterPasskey& p) {
        out.headline = "Enter the passkey the controller shows";
        out.ask = BtPromptRows::Ask::EnterPasskey;
        out.gen = p.gen;
    }

    void on(const Prompt::EnterPin& p) {
        out.headline = "Enter the controller's PIN";
        out.ask = BtPromptRows::Ask::EnterPin;
        out.gen = p.gen;
    }

    void on(const Prompt::Authorize& p) {
        out.headline = "Allow this controller to connect?";
        out.ask = BtPromptRows::Ask::AcceptReject;
        out.gen = p.gen;
    }

    void misrouted(const Prompt&) {}
};

}

BtPromptRows BtPromptRows::of(const svc::BtPrompt& prompt) {
    Projector projector;
    infra::dispatch<infra::AllRouted>(prompt, projector);
    return std::move(projector.out);
}

}
