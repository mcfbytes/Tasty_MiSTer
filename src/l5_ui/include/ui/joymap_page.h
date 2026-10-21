// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/input_wire.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/input_service.h"
#include "svc/mapping_wizard.h"
#include "ui/page.h"

namespace mister::ui {

struct JoyMapPrompt {
    std::uint8_t word = 0;
    char name[32] = {};

    std::string_view text() const noexcept { return std::string_view{name}; }
};

[[nodiscard]] std::vector<JoyMapPrompt> build_joymap_prompts(
    bool system_pass,
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

    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    Transition on_tick(PageContext& ctx) override;

    bool wake_every_round() const override {
        TASTY_SEAT_BODY(JoyMapPage);
        return !done_;
    }

    bool allows_global_hotkeys() const override {
        TASTY_SEAT_BODY(JoyMapPage);
        return false;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(JoyMapPage);
        return refusals_;
    }

    const std::vector<JoyMapPrompt>& prompts() const noexcept { return prompts_; }

    std::size_t current() const noexcept { return cur_; }

    bool locked() const noexcept { return locked_; }

    bool done() const noexcept { return done_; }

    bool saved() const noexcept { return saved_; }

    std::uint32_t arm_token() const noexcept { return token_; }

    svc::MappingWizard wizard() const noexcept { return wizard_; }

    std::span<const std::uint32_t> words() const noexcept { return blob_; }

private:
    enum class RowRole : std::uint8_t { Line, Legend };

    template <class Visit>
    void for_each_row(Visit&& visit) const;
    void render_row(OsdRenderer& out, std::uint8_t row, std::string_view text) const;

    std::uint16_t captured_code(std::size_t i) const noexcept;

    void arm_next();
    void rebegin();
    void disarm();
    void record(const JoyMapPrompt& p, std::uint16_t code);
    bool duplicate_in_segment(const JoyMapPrompt& p, std::uint16_t code) const;
    Transition finish(bool save);

    Spec spec_;
    std::vector<JoyMapPrompt> prompts_;
    std::array<std::uint32_t, 32> blob_{};
    std::size_t cur_ = 0;
    std::uint32_t token_ = 0;
    bool armed_ = false;
    bool done_ = false;
    bool saved_ = false;

    bool locked_ = false;
    app::CaptureHit lock_{};

    svc::MappingWizard wizard_{};
    app::CaptureHitCell::Reader hit_reader_{};
    std::uint32_t refusals_ = 0;
};

}
