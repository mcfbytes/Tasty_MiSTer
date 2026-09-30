// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_image_source.h"

namespace mister::app {

static_assert(SaveImageSource::kSlots == 7, "one backend initializer per announceable slot");

SaveImageSource::SaveImageSource(const svc::Vfs& vfs) noexcept
    : backends_{{vfs, bindings_[0].path}, {vfs, bindings_[1].path}, {vfs, bindings_[2].path},
                {vfs, bindings_[3].path}, {vfs, bindings_[4].path}, {vfs, bindings_[5].path},
                {vfs, bindings_[6].path}} {}

SaveImageSource::Binding* SaveImageSource::find(proto::SlotIndex slot) {
    if (slot.v >= kSlots) return nullptr;
    return &bindings_[slot.v];
}

const SaveImageSource::Binding* SaveImageSource::find(proto::SlotIndex slot) const {
    if (slot.v >= kSlots) return nullptr;
    return &bindings_[slot.v];
}

bool SaveImageSource::bind(proto::SlotIndex slot, std::string_view path, bool manual) noexcept {
    Binding* b = find(slot);
    if (b == nullptr) return false;

    if (b->attach != Attach::Detached) return false;
    b->exists = false;
    b->size = proto::FileSize{};
    b->blank = cores::BlankSaveSpec{};
    b->manual = !path.empty() && manual;
    b->close_owed = false;
    if (path.empty()) {
        b->path.path.clear();
        return true;
    }
    if (!b->path.path.assign(path)) {
        b->path.path.clear();
        return false;
    }
    return true;
}

void SaveImageSource::unbind_all() {
    for (unsigned i = 0; i < kSlots; ++i) {
        (void)bind(proto::SlotIndex{static_cast<std::uint8_t>(i)}, {});
    }
}

void SaveImageSource::set_blank_pattern(proto::SlotIndex slot, const cores::BlankSaveSpec& spec) {
    if (Binding* b = find(slot); b != nullptr) b->blank = spec;
}

std::string_view SaveImageSource::path_of(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    return b == nullptr ? std::string_view{} : b->path.path.view();
}

proto::SlotAttributes SaveImageSource::attributes(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    if (b == nullptr || b->path.path.view().empty()) return proto::SlotAttributes{};
    const bool pre = !b->manual;
    return proto::SlotAttributes{
        .writable = pre || b->exists, .growable = pre, .deferred_create = pre && !b->exists};
}

bool SaveImageSource::manual(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b != nullptr && b->manual;
}

void SaveImageSource::owe_close(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (Binding* b = find(slot); b != nullptr) b->close_owed = true;
}

bool SaveImageSource::close_owed(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b != nullptr && b->close_owed;
}

proto::FileSize SaveImageSource::size_of(proto::SlotIndex slot) const {
    const Binding* b = find(slot);
    if (b == nullptr || !b->exists) return proto::FileSize{};
    return b->size;
}

void SaveImageSource::note_attached(proto::SlotIndex slot, bool exists,
                                    proto::FileSize size) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    Binding* b = find(slot);
    if (b == nullptr) return;
    b->exists = exists;
    b->size = exists ? size : proto::FileSize{};
    b->attach = Attach::Attached;
}

void SaveImageSource::note_detached(proto::SlotIndex slot) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    Binding* b = find(slot);
    if (b == nullptr) return;
    b->exists = false;
    b->size = proto::FileSize{};

    b->path.path.clear();
    b->blank = cores::BlankSaveSpec{};
    b->manual = false;
    b->close_owed = false;
    b->attach = Attach::Detached;
}

SaveImageSource::Attach SaveImageSource::attach_state(proto::SlotIndex slot) const noexcept {
    const Binding* b = find(slot);
    return b == nullptr ? Attach::Detached : b->attach;
}

void SaveImageSource::mark(proto::SlotIndex slot, Attach state) noexcept {
    TASTY_SEAT_BODY(SaveImageSource);
    if (Binding* b = find(slot); b != nullptr) b->attach = state;
}

svc::IStorageBackend* SaveImageSource::backend(proto::SlotIndex slot) noexcept {
    if (slot.v >= kSlots) return nullptr;
    return &backends_[slot.v];
}

bool SaveImageSource::fill_blank(proto::SlotIndex slot, proto::Lba lba,
                                 std::span<std::uint8_t> window) {
    const Binding* b = find(slot);
    if (b == nullptr || !b->blank.declared()) return false;
    cores::render_blank(b->blank, static_cast<std::uint64_t>(lba.v) * kBlankBlockBytes, window);
    return true;
}

}  // namespace mister::app
