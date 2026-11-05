// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/joymap_page.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>

#include "app/identity_latch.h"
#include "app/page_description.h"
#include "infra/endian.h"
#include "svc/button_map.h"
#include "svc/vfs.h"
#include "ui/draw_classic.h"
#include "ui/navigator.h"
#include "ui/osd_renderer.h"
#include "ui/key_decoder.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr std::string_view kWordNames[12] = {
    "RIGHT",
    "LEFT",
    "DOWN",
    "UP",
    "BUTTON A",
    "BUTTON B",
    "BUTTON X",
    "BUTTON Y",
    "BUTTON L",
    "BUTTON R",
    "SELECT",
    "START"
};

constexpr std::string_view kMenuOk = "Menu: OK";
constexpr std::string_view kMenuBack = "Menu: Back";

constexpr std::uint8_t kCntOk = 21;
constexpr std::uint8_t kCntEsc = 22;
constexpr std::uint8_t kMenuFuncWord = 23;
constexpr std::uint8_t kSegment = 12;
constexpr std::size_t kDpad = 4;
constexpr std::size_t kMaxNames = 28;
constexpr std::uint8_t kClassicRows = 16;
static_assert(kClassicRows <= kMaxPageRows, "the classic rows must fit the walk's row table");

static_assert(kMaxNames * sizeof(JoyMapPrompt::name) - 1 <= app::SessionIdentity::kJNamesCap - 1);

bool is_make(std::uint32_t raw, std::uint32_t key) noexcept {
    constexpr std::uint32_t kCodeMask = std::numeric_limits<std::uint16_t>::max();
    return (raw & KeyDecoder::kUpstroke) == 0 && (raw & kCodeMask) == key;
}

}

std::vector<JoyMapPrompt> build_joymap_prompts(bool system_pass, std::string_view j_names) {
    std::vector<JoyMapPrompt> out;
    const auto add = [&out](std::uint8_t word, std::string_view name) {
        JoyMapPrompt p{};
        p.word = word;
        const std::size_t n = name.size() < sizeof p.name - 1 ? name.size() : sizeof p.name - 1;
        std::memcpy(p.name, name.data(), n);
        p.name[n] = '\0';
        out.push_back(p);
    };
    if (system_pass) {
        for (std::uint8_t w = 0; w < std::size(kWordNames); ++w)
            add(w, kWordNames[w]);
        add(kCntOk, kMenuOk);
        add(kCntEsc, kMenuBack);
        return out;
    }
    for (std::uint8_t w = 0; w < kDpad; ++w)
        add(w, kWordNames[w]);

    std::size_t count = 0;
    std::size_t begin = 0;
    while (count < kMaxNames && begin < j_names.size()) {
        const std::size_t comma = j_names.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? j_names.size() : comma;
        const std::string_view field = j_names.substr(begin, end - begin);
        if (field.empty())
            break;
        add(static_cast<std::uint8_t>(kDpad + count), field);
        ++count;
        if (comma == std::string_view::npos)
            break;
        begin = comma + 1;
    }

    if (count == 0) {
        constexpr std::size_t kGenericButtons = 4;
        for (std::uint8_t w = kDpad; w < kDpad + kGenericButtons; ++w)
            add(w, kWordNames[w]);
    }
    return out;
}

JoyMapPage::JoyMapPage(Spec spec) : spec_(static_cast<Spec&&>(spec)) {
    TASTY_SEAT_BODY(JoyMapPage);
    const bool system_pass = spec_.front_end || spec_.core.empty();
    spec_.front_end = system_pass;
    prompts_ = build_joymap_prompts(system_pass, spec_.j_names);
    (void)wizard_.begin(svc::DeviceIdentity{}, svc::MapKind::Joystick);
    if (!spec_.wire || prompts_.empty()) {
        done_ = true;
        ++refusals_;
        return;
    }

    token_ = spec_.wire->capture_arm().sample().value.token;
    arm_next();
}

JoyMapPage::~JoyMapPage() {
    disarm();
}

void JoyMapPage::arm_next() {
    if (++token_ == 0)
        ++token_;
    spec_.wire->capture_arm().publish(app::CaptureArm{token_, 1});
    armed_ = true;
    spec_.wire->wake();
}

void JoyMapPage::disarm() {
    if (!spec_.wire || !armed_)
        return;
    spec_.wire->capture_arm().publish(app::CaptureArm{token_, 0});
    armed_ = false;
    spec_.wire->wake();
}

void JoyMapPage::record(const JoyMapPrompt& p, std::uint16_t code) {
    constexpr unsigned kHalfBits = std::numeric_limits<std::uint16_t>::digits;
    if (p.word == kCntOk) {
        blob_[kMenuFuncWord] = code;
    } else if (p.word == kCntEsc) {
        blob_[kMenuFuncWord] = (static_cast<std::uint32_t>(code) << kHalfBits)
            | blob_[kMenuFuncWord];
    } else if (p.word < blob_.size()) {
        blob_[p.word] = code;
    }
}

std::uint16_t JoyMapPage::captured_code(std::size_t i) const noexcept {
    constexpr unsigned kHalfBits = std::numeric_limits<std::uint16_t>::digits;
    constexpr std::uint32_t kHalfMask = std::numeric_limits<std::uint16_t>::max();
    if (i >= prompts_.size())
        return 0;
    const JoyMapPrompt& p = prompts_[i];
    if (p.word == kCntOk)
        return static_cast<std::uint16_t>(blob_[kMenuFuncWord] & kHalfMask);
    if (p.word == kCntEsc)
        return static_cast<std::uint16_t>((blob_[kMenuFuncWord] >> kHalfBits) & kHalfMask);
    if (p.word < blob_.size())
        return static_cast<std::uint16_t>(blob_[p.word] & kHalfMask);
    return 0;
}

bool JoyMapPage::duplicate_in_segment(const JoyMapPrompt& p, std::uint16_t code) const {
    const std::uint8_t seg = p.word >= kSegment ? kSegment : std::uint8_t{0};
    for (std::uint8_t w = seg; w < p.word && w < blob_.size(); ++w) {
        if (blob_[w] == code)
            return true;
    }
    return false;
}

Transition JoyMapPage::finish(bool save) {
    disarm();
    done_ = true;
    if (!save) {
        wizard_.cancel();
        return Transition{Transition::Kind::Pop};
    }
    if (!locked_ || !spec_.files || lock_.id_clipped) {

        if (locked_)
            ++refusals_;
        return Transition{Transition::Kind::Pop};
    }
    const svc::MapStore store{*spec_.files};
    svc::ButtonMap map;
    map.kind = svc::MapKind::Joystick;
    map.blob.resize(blob_.size() * sizeof(std::uint32_t));
    infra::store_le_run<std::uint32_t>(
        std::span<const std::uint32_t>{blob_},
        std::span<std::byte>{map.blob}
    );
    svc::DeviceIdentity id{};
    id.vid = lock_.vid;
    id.pid = lock_.pid;
    id.id = std::string(lock_.id.view());
    id.unique_hash = lock_.unique_hash;
    id.unique_filenames = lock_.unique_filenames;
    const std::string_view core = spec_.front_end ? std::string_view{}
                                                  : std::string_view{spec_.core};
    if (store.save(core, id, map, lock_.mod)) {
        saved_ = true;

        spec_.wire->request(app::InputWire::kReqHotplug);
        spec_.wire->wake();
    } else {
        ++refusals_;
    }
    return Transition{Transition::Kind::Pop};
}

Transition JoyMapPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(JoyMapPage);
    if (ev.kind != UiEvent::Kind::Key)
        return Transition{};
    if (done_) {
        if (ev.buttons.has_any({UiEvent::Select | UiEvent::Menu})) {
            return Transition{Transition::Kind::Pop};
        }
        return Transition{};
    }

    if (is_make(ev.raw.v, KEY_SPACE)) {
        if (!locked_)
            return Transition{};
        ++cur_;
        if (cur_ >= prompts_.size())
            return finish(true);
        rebegin();
        arm_next();
        return Transition{};
    }

    if (is_make(ev.raw.v, KEY_ENTER) || is_make(ev.raw.v, KEY_KPENTER)) {
        return finish(locked_);
    }
    if (ev.buttons.has_any({UiEvent::Menu}))
        return finish(false);
    return Transition{};
}

Transition JoyMapPage::on_tick(PageContext&) {
    TASTY_SEAT_BODY(JoyMapPage);
    if (done_ || !spec_.wire)
        return Transition{};
    app::CaptureHit hit{};
    if (!hit_reader_.take_if_changed(spec_.wire->capture_hit(), hit))
        return Transition{};
    if (hit.token != token_)
        return Transition{};
    if (!locked_) {
        locked_ = true;
        lock_ = hit;
        svc::DeviceIdentity id{};
        id.vid = hit.vid;
        id.pid = hit.pid;
        id.id = std::string(hit.id.view());
        wizard_.on_device(id);
    } else if (hit.device_ordinal != lock_.device_ordinal || hit.id.view() != lock_.id.view()) {

        arm_next();
        return Transition{};
    }
    if (duplicate_in_segment(prompts_[cur_], hit.code)) {
        arm_next();
        return Transition{};
    }
    wizard_.on_button(hit.code);
    (void)wizard_.advance();
    (void)wizard_.advance();
    record(prompts_[cur_], hit.code);
    ++cur_;
    if (cur_ >= prompts_.size())
        return finish(true);
    rebegin();
    arm_next();
    return Transition{};
}

void JoyMapPage::rebegin() {
    svc::DeviceIdentity id{};
    id.vid = lock_.vid;
    id.pid = lock_.pid;
    id.id = std::string(lock_.id.view());
    (void)wizard_.begin(id, svc::MapKind::Joystick);
}

template <class Visit>
void JoyMapPage::for_each_row(Visit&& visit) const {
    char s[40];
    s[0] = '\0';
    if (!done_ && cur_ < prompts_.size()) {

        const std::string_view name = prompts_[cur_].text();
        constexpr std::string_view kPrompt = "Press: ";
        constexpr int kRowColumns = 30;
        int pad = (kRowColumns - (static_cast<int>(name.size()) + static_cast<int>(kPrompt.size())))
            / 2;
        if (pad < 0)
            pad = 0;
        std::snprintf(
            s,
            sizeof s,
            "%*sPress: %.*s",
            pad,
            "",
            static_cast<int>(name.size()),
            name.data()
        );
    }
    char idrow[40];
    idrow[0] = '\0';
    if (locked_) {
        std::snprintf(
            idrow,
            sizeof idrow,
            "   Joystick ID: %04x:%04x",
            static_cast<unsigned>(lock_.vid.v),
            static_cast<unsigned>(lock_.pid.v)
        );
    }
    constexpr std::uint8_t kPromptRow = 3;
    constexpr std::uint8_t kIdRow = 5;
    constexpr std::uint8_t kSkipRow = 7;
    constexpr std::uint8_t kCancelRow = 8;
    constexpr std::uint8_t kFinishRow = 9;
    for (std::uint8_t r = 0; r < kClassicRows; ++r) {
        switch (r) {
            case kPromptRow:
                visit(r, std::string_view{s}, RowRole::Line);
                break;
            case kIdRow:
                visit(r, std::string_view{idrow}, RowRole::Line);
                break;
            case kSkipRow:
                visit(
                    r,
                    locked_ ? std::string_view{"        Space \x16 Skip"} : std::string_view{},
                    RowRole::Legend
                );
                break;
            case kCancelRow:
                visit(r, std::string_view{"          Esc \x16 Cancel"}, RowRole::Legend);
                break;
            case kFinishRow:
                visit(r, std::string_view{"        Enter \x16 Finish"}, RowRole::Legend);
                break;
            default:
                visit(r, std::string_view{}, RowRole::Line);
                break;
        }
    }
}

bool JoyMapPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(JoyMapPage);
    (void)out.page.title.assign("Define buttons");
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    struct Row {
        std::uint8_t row;
        std::string text;
        RowRole role;
    };

    std::vector<Row> rows;
    for_each_row([&](std::uint8_t row, std::string_view text, RowRole role) {
        rows.push_back(Row{row, std::string{text}, role});
    });

    std::string legend;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const bool legend_row = row.role == RowRole::Legend && !row.text.empty();
        if (legend_row) {

            std::string_view text{row.text};
            while (!text.empty() && text.front() == ' ')
                text.remove_prefix(1);
            constexpr std::string_view kArrow = " \x16 ";
            const std::size_t arrow = text.find(kArrow);
            if (!legend.empty())
                legend += "   ";
            legend += text.substr(0, arrow);
            if (arrow != std::string_view::npos) {
                legend += ' ';
                legend += text.substr(arrow + kArrow.size());
            }
        }
        PageRow& pr = out.rows[out.count++];
        pr.has_hd = false;
        pr.legend = legend_row;
        pr.classic = classic_row(
            OsdRow{row.row},
            row.text,
            false,
            false,
            std::nullopt,
            OsdRenderer::kArrowNone
        );
    }

    std::uint16_t hd_total = 0;
    const std::size_t hd_cap = std::min<std::size_t>(
        out.rows.size(),
        static_cast<std::size_t>(out.count) + app::PageDescription::kMaxRows
    );
    for (const Row& row : rows) {
        std::string_view text{row.text};
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        if (text.empty() || row.role == RowRole::Legend)
            continue;
        ++hd_total;
        if (out.count >= hd_cap)
            continue;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(text);
    }

    const std::size_t captured = cur_ < prompts_.size() ? cur_ : prompts_.size();
    for (std::size_t i = captured; i-- > 0;) {
        ++hd_total;
        if (out.count >= hd_cap)
            continue;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(prompts_[i].text());
        const std::uint16_t code = captured_code(i);
        char v[16];
        if (code == 0) {
            std::snprintf(v, sizeof v, "%s", "skipped");
        } else {
            std::snprintf(v, sizeof v, "0x%X", static_cast<unsigned>(code));
        }
        (void)r.hd.value.assign(std::string_view{v});
    }

    out.page.first = 0;
    out.page.total = hd_total;
    (void)out.page.footer.assign(legend);
    return true;
}

}
