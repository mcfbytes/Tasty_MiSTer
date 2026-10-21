// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_policy.h"

#include "app/process_integration.h"
#include "ui/bt_ops.h"
#include "ui/bt_pair_policy.h"
#include "ui/script_file_policy.h"
#include "ui/system_page.h"

namespace mister::ui {

namespace {

std::string_view basename_of(std::string_view path) noexcept {
    const auto pos = path.find_last_of('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

}

IScriptPolicy::OpenGate ScriptFilePolicy::gate_open(infra::OptRef<IBtOps>) const {
    return OpenGate::Proceed;
}

bool ScriptFilePolicy::is_pairing() const noexcept {
    return false;
}

Ex<IScriptPolicy::Spawn> ScriptFilePolicy::resolve(std::string_view argv0) const {
    if (argv0.empty())
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    return Spawn{std::string(argv0), std::string(basename_of(argv0))};
}

void ScriptFilePolicy::before_spawn(bool, infra::OptRef<IBtOps>) const {}

void ScriptFilePolicy::stop(std::string_view title) const {
    if (!title.empty())
        (void)app::proc::killall(std::string(title).c_str());
}

std::string_view ScriptFilePolicy::running_footer() const noexcept {
    return "           Cancel";
}

bool ScriptFilePolicy::renews_on_back() const noexcept {
    return false;
}

Transition ScriptFilePolicy::exit_transition() const {
    return Transition{
        Transition::Kind::Pop,
        PageId::None,
        ItemId{static_cast<std::uint16_t>(SystemItem::Scripts)}
    };
}

IScriptPolicy::OpenGate BtPairPolicy::gate_open(infra::OptRef<IBtOps> bt) const {

    if (!bt)
        return OpenGate::Refuse;
    const auto up = bt->adapter_up();
    if (!up)
        return OpenGate::Refuse;
    return *up ? OpenGate::Proceed : OpenGate::NoAdapter;
}

bool BtPairPolicy::is_pairing() const noexcept {
    return true;
}

Ex<IScriptPolicy::Spawn> BtPairPolicy::resolve(std::string_view) const {
    return Spawn{"/usr/sbin/btpair", "Bluetooth"};
}

void BtPairPolicy::before_spawn(bool bt_reset, infra::OptRef<IBtOps> bt) const {
    if (bt_reset && bt)
        (void)bt->reset_adapter();
}

void BtPairPolicy::stop(std::string_view) const {
    (void)app::proc::killall_sigint_bt();
}

std::string_view BtPairPolicy::running_footer() const noexcept {
    return "           Finish";
}

bool BtPairPolicy::renews_on_back() const noexcept {
    return true;
}

Transition BtPairPolicy::exit_transition() const {
    return Transition{Transition::Kind::CloseAll};
}

const IScriptPolicy& script_policy_for(ScriptKind kind) noexcept {

    static constexpr ScriptFilePolicy kScript{};
    static constexpr BtPairPolicy kBtPair{};
    switch (kind) {
        case ScriptKind::BtPair:
            return kBtPair;
        case ScriptKind::Script:
            return kScript;
    }
    return kScript;
}

}
