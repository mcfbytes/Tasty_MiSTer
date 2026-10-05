// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "app/companion_host.h"
#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/load_walk.h"
#include "app/path_text.h"
#include "cores/companion_plan.h"
#include "cores/loader.h"
#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class CompanionWalk {
    TASTY_SEAT_EXEMPT(main);

public:
    enum class Pass : std::uint8_t { Stepped, Waiting, Done };
    enum class Pc : std::uint8_t { Bind, Before, Load, After, Over };

    struct Host {
        LoadWalk::Host load;
        CompanionHost::Binds* binds;
    };

    [[nodiscard]] static Ex<CompanionWalk> start(const cores::CompanionPlan& plan,
                                                 std::uint16_t gen,
                                                 std::unique_ptr<cores::ILoader> loader);

    [[nodiscard]] Pass step(Host& h);
    [[nodiscard]] bool wants_pass(const LinkTxChannel& inbox,
                                  const FileTxLevelCell* level) const noexcept;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }
    [[nodiscard]] bool loads() const noexcept { return plan_.loads; }

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool bound() const noexcept { return bound_; }

private:
    CompanionWalk(const cores::CompanionPlan& plan, std::uint16_t gen,
                  std::unique_ptr<cores::ILoader> loader) noexcept
        : plan_(plan), loader_(std::move(loader)), gen_(gen) {}

    [[nodiscard]] Pass bind_(Host& h);
    [[nodiscard]] Pass write_(Host& h, const std::array<std::uint16_t, 3>& words,
                              proto::MailboxPoll poll, Pc next);
    [[nodiscard]] Pass load_(Host& h);

    cores::CompanionPlan plan_;
    PathText stem_{};
    PathText source_{};
    std::unique_ptr<cores::ILoader> loader_;
    std::optional<LoadWalk> walk_{};
    std::uint16_t gen_ = 0;
    bool loaded_ = false;
    bool bound_ = false;
    Pc pc_ = Pc::Bind;
};

}  // namespace mister::app
