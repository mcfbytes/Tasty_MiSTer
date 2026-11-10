// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/input_wire.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/input_service.h"
#include "svc/mapping_wizard.h"
#include "ui/page.h"
#include "ui/joymap_blob.h"

namespace mister::ui {

enum class MapScope : std::uint8_t { System, Core };

struct JoyMapPrompt {
    BlobWord word{};
    FixedStr<32, StrFit::Clip> name{};

    std::string_view text() const noexcept { return name.view(); }
};

[[nodiscard]] std::vector<JoyMapPrompt> build_joymap_prompts(
    MapScope scope,
    std::string_view j_names
);

class JoyMapPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {
        infra::OptRef<app::InputWire> wire{};
        infra::OptRef<const svc::Vfs> files{};
        bool front_end = false;
        std::string core;
        std::string j_names;
    };

    explicit JoyMapPage(Spec spec);

    ~JoyMapPage() override;
    JoyMapPage(const JoyMapPage&) = delete;
    JoyMapPage& operator=(const JoyMapPage&) = delete;

    PageId id() const override {
        TASTY_SEAT_BODY(JoyMapPage);
        return PageId::JoyMap;
    }

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;
    Transition on_tick(PageContext& ctx) override;

    PageFacts facts() const override {
        TASTY_SEAT_BODY(JoyMapPage);
        PageFacts f{};
        f.wake_every_round = !done_;
        f.allows_global_hotkeys = false;
        return f;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(JoyMapPage);
        return refusals_;
    }

    const std::vector<JoyMapPrompt>& prompts() const noexcept { return prompts_; }

    std::size_t current() const noexcept { return cur_; }

    bool locked() const noexcept { return lock_.has_value(); }

    bool done() const noexcept { return done_; }

    bool saved() const noexcept { return saved_; }

    app::CaptureToken arm_token() const noexcept { return token_; }

    svc::MappingWizard wizard() const noexcept { return wizard_; }

private:
    enum class RowRole : std::uint8_t { Line, Legend };

    template <class Visit>
    void for_each_row(Visit&& visit) const;

    svc::EvdevCode captured_code(std::size_t i) const noexcept;

    void arm_next();
    void rebegin();
    void disarm();

    bool wizard_advance_into(svc::MappingWizard::Step want);
    void record(const JoyMapPrompt& p, svc::EvdevCode code);
    bool duplicate_in_segment(const JoyMapPrompt& p, svc::EvdevCode code) const;
    Transition finish(bool save);

    Spec spec_;
    MapScope scope_ = MapScope::Core;
    std::vector<JoyMapPrompt> prompts_;
    JoyMapBlob blob_{};
    std::size_t cur_ = 0;
    app::CaptureToken token_{};
    bool armed_ = false;
    bool done_ = false;
    bool saved_ = false;

    std::optional<app::CaptureHit> lock_;

    svc::MappingWizard wizard_{};
    app::CaptureHitCell::Reader hit_reader_{};
    std::uint32_t refusals_ = 0;
};

}
