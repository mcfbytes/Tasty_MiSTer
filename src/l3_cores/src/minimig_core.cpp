// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig_core.h"

#include <array>

namespace mister::cores {
namespace {

constexpr std::array<Mm2PushRow, 1> kMm2Rows{{
    {0x00, 0, 1, "req push-table shape; rows transcribed at the minimig pass"},
}};

}

std::span<const Mm2PushRow> mm2_push_table() {
    return kMm2Rows;
}

Ex<void> MinimigCore::apply_configuration(const ConfigBlob& blob) {

    const bool old_format = blob.bytes.size() == kConfigBlobOldSize;
    if (!old_format && blob.bytes.size() > kConfigBlobMaxSize) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(blob.bytes.size())}
        );
    }

    (void)old_format;
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::akiko_service(hal::SpiWord status_word) {
    status_cache_ = status_word;

    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::a2065_drain() {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::mfm_write_track(std::uint8_t, std::uint8_t, std::span<const std::byte>) {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::enqueue_keycode(proto::KeyCode) {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::do_adopt(const CoreAdopt&) {

    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::do_reset() {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::on_mount(IoIndex, const MountedPath&) {
    return unimplemented(ERR_SITE());
}

Ex<void> MinimigCore::on_osd_closed() {

    return {};
}

}
