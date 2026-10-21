// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "app/recents_store.h"
#include "app/remembered_path.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "ui/browser_host.h"
#include "ui/browser_session.h"
#include "ui/page.h"

namespace mister::app {
class InfoSink;
class NameConfig;
class RomsetAliasReader;
class UiRequestRing;
}

namespace mister::cores {
struct FileSlot;
}

namespace mister::svc {
class Vfs;
}

namespace mister::ui {

struct BrowserLaunch;
struct BuildEnv;
class OptionItem;

class SlotPickHost final : private IBrowserHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Env {
        TASTY_SEAT_RESIDENT(Ui);

        explicit Env(const BuildEnv& env);

        std::string core_dir;
        std::string cue_dir;
        app::RememberedStem file_stem;

        std::span<const cores::FileSlot> slots;
        bool image_no_zip;
        proto::BindGeneration conf_gen;
    };

    struct Borrows {
        infra::OptRef<app::UiRequestRing> asks;
        infra::OptRef<BrowserLaunch> browser;
        infra::OptRef<app::RecentsStore> recents;
        infra::OptRef<app::NameConfig> names;
        infra::OptRef<app::InfoSink> info;
        infra::OptRef<const svc::Vfs> files;
    };

    struct RecentsRef {
        app::RecentsStore::ListId id{};
        std::string core;
    };

    SlotPickHost(const Env& env, Borrows b);
    ~SlotPickHost() override;
    SlotPickHost(const SlotPickHost&) = delete;
    SlotPickHost& operator=(const SlotPickHost&) = delete;

    Transition open(const OptionItem& it, bool offer_recent = false);
    Transition open_recents(const OptionItem& it);

    Transition pick(const OptionItem& it, std::string_view path, std::string_view label);

    Transition eject(const OptionItem& it);

    [[nodiscard]] RecentsRef recents_ref(const OptionItem& it) const;
    [[nodiscard]] bool has_recents(const OptionItem& it) const;

    std::uint32_t refusals() const noexcept { return refusals_; }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    Transition on_picked(const BrowserEntry& e, std::string_view path) override;
    Transition on_cancel() override;
    Transition on_eject() override;
    Transition on_clear() override;

    BrowserSession::Request browser_request(const OptionItem& it, bool offer_recent);

    [[nodiscard]] std::string_view browse_home_(const OptionItem& it) const noexcept;

    void arm_(const OptionItem& it) noexcept;

    void unmount_pending_();
    app::RecentsStore::ListId recents_id_for_pending() const noexcept;
    void ask_mount(std::uint8_t slot, std::string_view path);

    const Env& env_;
    const Borrows b_;
    std::unique_ptr<app::RomsetAliasReader> romsets_;

    bool pending_mounts_ = false;
    std::uint8_t pending_slot_ = 0;
    proto::ItemOrdinal pending_item_{};
    proto::BindGeneration pending_gen_{};
    bool pending_store_ = false;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
