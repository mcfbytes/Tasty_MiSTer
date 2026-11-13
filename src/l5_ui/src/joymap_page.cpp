// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/joymap_page.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
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

constexpr std::size_t kDpad = 4;
constexpr std::size_t kMaxNames = 28;
static_assert(kDpad + kMaxNames <= JoyMapBlob::kWords, "every prompt's word is a blob word");

static_assert(kFullOsdRows.v <= kMaxPageRows, "the classic rows must fit the walk's row table");

constexpr int kPromptColumns = 30;

constexpr proto::OsdRow kPromptRow{3};
constexpr proto::OsdRow kIdRow{5};
constexpr proto::OsdRow kSkipRow{7};
constexpr proto::OsdRow kCancelRow{8};
constexpr proto::OsdRow kFinishRow{9};

static_assert(
    kMaxNames * decltype(JoyMapPrompt::name)::kCapacity + (kMaxNames - 1)
    <= app::SessionIdentity::kJNamesCap - 1
);

}

std::vector<JoyMapPrompt> build_joymap_prompts(MapScope scope, std::string_view j_names) {
    std::vector<JoyMapPrompt> out;
    const auto add = [&out](BlobWord word, std::string_view name) {
        JoyMapPrompt p{};
        p.word = word;
        (void)p.name.assign(name);
        out.push_back(p);
    };

    BlobWord word{};
    if (scope == MapScope::System) {
        for (const std::string_view name : kWordNames) {
            add(word, name);
            word = word.next();
        }
        add(JoyMapBlob::kCntOk, kMenuOk);
        add(JoyMapBlob::kCntEsc, kMenuBack);
        return out;
    }
    for (std::size_t w = 0; w < kDpad; ++w) {
        add(word, kWordNames[w]);
        word = word.next();
    }
    const BlobWord first_button = word;

    std::size_t count = 0;
    std::size_t begin = 0;
    while (count < kMaxNames && begin < j_names.size()) {
        const std::size_t comma = j_names.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? j_names.size() : comma;
        const std::string_view field = j_names.substr(begin, end - begin);
        if (field.empty())
            break;
        add(word, field);
        word = word.next();
        ++count;
        if (comma == std::string_view::npos)
            break;
        begin = comma + 1;
    }

    if (count == 0) {
        constexpr std::size_t kGenericButtons = 4;
        word = first_button;
        for (std::size_t w = kDpad; w < kDpad + kGenericButtons; ++w) {
            add(word, kWordNames[w]);
            word = word.next();
        }
    }
    return out;
}

JoyMapPage::JoyMapPage(Spec spec)
    : spec_(static_cast<Spec&&>(spec)),
      scope_(spec_.front_end || spec_.core.empty() ? MapScope::System : MapScope::Core) {
    TASTY_SEAT_BODY(JoyMapPage);
    prompts_ = build_joymap_prompts(scope_, spec_.j_names);
    wizard_.begin(svc::DeviceIdentity{}, svc::MapKind::Joystick);
    if (!spec_.ends || prompts_.empty()) {
        done_ = true;
        refusals_.add();
        return;
    }

    token_ = spec_.ends->arm.sample().value.token;
    arm_next();
}

JoyMapPage::~JoyMapPage() {
    disarm();
}

void JoyMapPage::arm_next() {
    token_ = token_.next();
    spec_.ends->arm.publish(app::CaptureArm{.token = token_, .armed = true});
    armed_ = true;
    spec_.ends->input_ctrl.kick();
}

void JoyMapPage::disarm() {
    if (!spec_.ends || !armed_)
        return;
    spec_.ends->arm.publish(app::CaptureArm{.token = token_, .armed = false});
    armed_ = false;
    spec_.ends->input_ctrl.kick();
}

void JoyMapBlob::store(BlobWord word, svc::EvdevCode code) noexcept {
    std::uint32_t& menu = infra::at_index(blob_, kMenuFuncWord);
    if (word == kCntOk) {
        menu = svc::packed_word(svc::PackedKeys::lone(code));
    } else if (word == kCntEsc) {
        svc::PackedKeys pair = svc::packed_keys_of(menu);
        pair.hi = pair.hi.or_bits(code);
        menu = svc::packed_word(pair);
    } else if (word.indexes(blob_)) {
        infra::at_index(blob_, word) = svc::packed_word(svc::PackedKeys::lone(code));
    }
}

svc::EvdevCode JoyMapBlob::code(BlobWord word) const noexcept {
    const svc::PackedKeys menu = svc::packed_keys_of(infra::at_index(blob_, kMenuFuncWord));
    if (word == kCntOk)
        return menu.lo;
    if (word == kCntEsc)
        return menu.hi;
    if (word.indexes(blob_))
        return svc::packed_keys_of(infra::at_index(blob_, word)).lo;
    return svc::EvdevCode{};
}

bool JoyMapBlob::duplicate_in(BlobWord word, svc::EvdevCode code) const noexcept {
    const BlobWord seg = word >= kSegment ? kSegment : BlobWord{};
    for (BlobWord w = seg; w < word && w.indexes(blob_); w = w.next()) {
        if (svc::packed_keys_of(infra::at_index(blob_, w)) == svc::PackedKeys::lone(code))
            return true;
    }
    return false;
}

void JoyMapPage::record(const JoyMapPrompt& p, svc::EvdevCode code) {
    blob_.store(p.word, code);
}

svc::EvdevCode JoyMapPage::captured_code(std::size_t i) const noexcept {
    if (i >= prompts_.size())
        return {};
    return blob_.code(prompts_[i].word);
}

bool JoyMapPage::duplicate_in_segment(const JoyMapPrompt& p, svc::EvdevCode code) const {
    return blob_.duplicate_in(p.word, code);
}

Transition JoyMapPage::finish(bool save) {
    disarm();
    done_ = true;
    if (!save) {
        wizard_.cancel();
        return Transition::pop();
    }
    if (!lock_ || !spec_.files || lock_->id_clipped) {

        if (lock_)
            refusals_.add();
        return Transition::pop();
    }
    const svc::MapStore store{*spec_.files};
    svc::ButtonMap map;
    map.kind = svc::MapKind::Joystick;
    map.blob.resize(blob_.words().size() * sizeof(std::uint32_t));
    infra::store_le_run<std::uint32_t>(blob_.words(), std::span<std::byte>{map.blob});
    svc::DeviceIdentity id{};
    id.vid = lock_->vid;
    id.pid = lock_->pid;
    id.id = std::string(lock_->id.view());
    id.unique_hash = lock_->unique_hash;
    id.unique_filenames = lock_->unique_filenames;
    const std::string_view core = scope_ == MapScope::System ? std::string_view{}
                                                             : std::string_view{spec_.core};
    if (store.save(core, id, map, lock_->mod)) {
        saved_ = true;

        spec_.ends->hotplug.request();
        spec_.ends->input_ctrl.kick();
    } else {
        refusals_.add();
    }
    return Transition::pop();
}

Transition JoyMapPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(JoyMapPage);
    if (done_) {
        if (ev.buttons.has_any({UiEvent::Select | UiEvent::Menu})) {
            return Transition::pop();
        }
        return Transition{};
    }

    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_SPACE})) {
        if (!lock_)
            return Transition{};
        ++cur_;
        if (cur_ >= prompts_.size())
            return finish(true);
        rebegin();
        arm_next();
        return Transition{};
    }

    if (KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_ENTER})
        || KeyDecoder::is_make(ev.raw, svc::EvdevCode{KEY_KPENTER})) {
        return finish(lock_.has_value());
    }
    if (ev.buttons.has_any({UiEvent::Menu}))
        return finish(false);
    return Transition{};
}

Transition JoyMapPage::on_tick(PageContext&) {
    TASTY_SEAT_BODY(JoyMapPage);
    if (done_ || !spec_.ends)
        return Transition{};
    app::CaptureHit hit{};
    if (!hit_reader_.take_if_changed(spec_.ends->hit, hit))
        return Transition{};
    if (hit.token != token_)
        return Transition{};
    if (!lock_) {
        lock_ = hit;
        svc::DeviceIdentity id{};
        id.vid = hit.vid;
        id.pid = hit.pid;
        id.id = std::string(hit.id.view());
        wizard_.on_device(id);
    } else if (hit.device_ordinal != lock_->device_ordinal || hit.id.view() != lock_->id.view()) {

        arm_next();
        return Transition{};
    }
    if (duplicate_in_segment(prompts_[cur_], hit.code)) {
        arm_next();
        return Transition{};
    }
    wizard_.on_button(hit.code);

    if (!wizard_advance_into(svc::MappingWizard::Step::Confirm)
        || !wizard_advance_into(svc::MappingWizard::Step::Persist)) {
        refusals_.add();
        rebegin();
        arm_next();
        return Transition{};
    }
    record(prompts_[cur_], hit.code);
    ++cur_;
    if (cur_ >= prompts_.size())
        return finish(true);
    rebegin();
    arm_next();
    return Transition{};
}

bool JoyMapPage::wizard_advance_into(svc::MappingWizard::Step want) {
    const auto step = wizard_.advance();
    return step && *step == want;
}

void JoyMapPage::rebegin() {
    svc::DeviceIdentity id{};
    id.vid = lock_->vid;
    id.pid = lock_->pid;
    id.id = std::string(lock_->id.view());
    wizard_.begin(id, svc::MapKind::Joystick);
}

template <class Visit>
void JoyMapPage::for_each_row(Visit&& visit) const {

    constexpr std::ptrdiff_t kRoom = static_cast<std::ptrdiff_t>(kClassicRowCap) - 1;
    std::array<char, kClassicRowCap> s{};
    if (!done_ && cur_ < prompts_.size()) {

        const std::string_view name = prompts_[cur_].text();
        constexpr std::string_view kPrompt = "Press: ";
        int pad = (kPromptColumns
                   - (static_cast<int>(name.size()) + static_cast<int>(kPrompt.size())))
            / 2;
        if (pad < 0)
            pad = 0;
        const auto out = std::format_to_n(
            s.data(),
            kRoom,
            "{:>{}}Press: {}",
            std::string_view{},
            pad,
            name
        );
        *out.out = '\0';
    }
    std::array<char, kClassicRowCap> idrow{};
    if (lock_) {
        const auto out = std::format_to_n(
            idrow.data(),
            kRoom,
            "   Joystick ID: {:0>4}:{:0>4}",
            lock_->vid.text().c_str(),
            lock_->pid.text().c_str()
        );
        *out.out = '\0';
    }
    for (const RowIndex r : rows(kFullOsdRows)) {
        const proto::OsdRow at = osd_row_at(r.before());
        if (at == kPromptRow) {
            visit(r, std::string_view{s.data()}, RowRole::Line);
        } else if (at == kIdRow) {
            visit(r, std::string_view{idrow.data()}, RowRole::Line);
        } else if (at == kSkipRow) {
            visit(
                r,
                lock_ ? std::string_view{"        Space \x16 Skip"} : std::string_view{},
                RowRole::Legend
            );
        } else if (at == kCancelRow) {
            visit(r, std::string_view{"          Esc \x16 Cancel"}, RowRole::Legend);
        } else if (at == kFinishRow) {
            visit(r, std::string_view{"        Enter \x16 Finish"}, RowRole::Legend);
        } else {
            visit(r, std::string_view{}, RowRole::Line);
        }
    }
}

bool JoyMapPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(JoyMapPage);
    (void)out.page.title.assign("Define buttons");
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    struct Drawn {
        RowIndex row;
        std::string text;
        RowRole role;
    };

    std::vector<Drawn> drawn;
    for_each_row([&](RowIndex row, std::string_view text, RowRole role) {
        drawn.push_back(Drawn{row, std::string{text}, role});
    });

    std::string legend;
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const Drawn& row = drawn[i];
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
            osd_row_at(row.row.before()),
            row.text,
            {.arrow = OsdRenderer::kArrowNone}
        );
    }

    RowCount hd_total{};
    const std::size_t hd_cap = std::min<std::size_t>(
        out.rows.size(),
        static_cast<std::size_t>(out.count) + app::PageDescription::kMaxRows
    );
    for (const Drawn& row : drawn) {
        std::string_view text{row.text};
        while (!text.empty() && text.front() == ' ')
            text.remove_prefix(1);
        if (text.empty() || row.role == RowRole::Legend)
            continue;
        hd_total += kOneRow;
        if (out.count >= hd_cap)
            continue;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(text);
    }

    const std::size_t captured = cur_ < prompts_.size() ? cur_ : prompts_.size();
    for (std::size_t i = captured; i-- > 0;) {
        hd_total += kOneRow;
        if (out.count >= hd_cap)
            continue;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(prompts_[i].text());
        const svc::EvdevCode code = captured_code(i);
        if (code.is_unset()) {
            (void)r.hd.value.assign("skipped");
        } else {
            (void)r.hd.value.assign("0x");
            (void)r.hd.value.append(code.text().c_str());
        }
    }

    out.page.first = RowIndex{};
    out.page.total = hd_total;
    (void)out.page.footer.assign(legend);
    return true;
}

}
