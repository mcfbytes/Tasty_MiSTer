// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/config_saves.h"

#include <cerrno>

#include <string>
#include <utility>
#include <vector>

#include "app/durable_write.h"
#include "app/ini_key_edit.h"
#include "app/interned_file.h"
#include "app/owner_push.h"
#include "app/stream_io.h"
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

ConfigSaves::ConfigSaves(const Wiring& w) noexcept
    : inbox_(w.inbox),
      rx_(w.rx),
      tally_(w.tally),
      owner_events_(w.owner_events),
      vfs_(w.vfs),
      status_cell_(w.status_cell),
      negotiated_(w.negotiated),
      sleeper_(w.sleeper) {}

void ConfigSaves::save_dips(CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    if (!order_save_ask(proto::SaveKind::Dips, 0, tag)) {
        publish_save_verdict_(false, tag);
    }
}

void ConfigSaves::save_slot(std::uint8_t slot, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    slot_save_slot_ = slot;
    if (!order_save_ask(proto::SaveKind::Slot, slot, tag)) {
        publish_save_verdict_(false, tag);
    }
}

void ConfigSaves::load_slot(std::uint8_t slot, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    slot_load_slot_ = slot;
    (void)order_save_ask(proto::SaveKind::SlotNames, slot, tag);
}

void ConfigSaves::set_ini_key(std::string_view key, std::string_view value) {
    TASTY_SEAT_BODY(ConfigSaves);
    constexpr std::string_view kIni = "MiSTer.ini";
    std::string text;
    auto f = vfs_.open(kIni, svc::OpenMode::ReadWhole);
    if (f) {
        const auto sz = (*f)->size();
        const auto buf = sz ? read_whole(**f, sz->v) : std::nullopt;
        if (!buf) {
            iwf_cell_.publish(++tally_.ini_write_failures);
            return;
        }
        text.assign(infra::as_text(std::span(*buf)));
    } else if (
        !(f.error().code == Errc::os && f.error().detail == ENOENT)
        && f.error().code != Errc::not_found
    ) {
        iwf_cell_.publish(++tally_.ini_write_failures);
        return;
    }
    const std::string out = ini_set_key(text, key, value);
    if (!write_durably_(kIni, std::as_bytes(std::span<const char>(out.data(), out.size())))) {
        iwf_cell_.publish(++tally_.ini_write_failures);
    }
}

bool ConfigSaves::intern_rel_at_(proto::FileId id, std::string_view rel) {
    std::vector<std::uint8_t> buf;
    if (!rel.empty()) {
        auto f = vfs_.open(rel, svc::OpenMode::ReadWhole);
        if (f) {
            auto sz = (*f)->size();
            if (sz && sz->v != 0) {
                buf.resize(static_cast<std::size_t>(sz->v));
                std::size_t off = 0;
                bool ok = true;
                while (off < buf.size()) {
                    auto n = (*f)->read_at(
                        off,
                        std::as_writable_bytes(std::span<std::uint8_t>(buf).subspan(off))
                    );
                    if (!n || *n == 0) {
                        ok = false;
                        break;
                    }
                    off += *n;
                }
                if (!ok)
                    buf.clear();
            }
        }
    }
    const bool has_bytes = !buf.empty();
    return inbox_.intern_file_at(id, std::move(buf), ".CFG", rel, 0).has_value() && has_bytes;
}

void ConfigSaves::order_slot_previews(const std::byte* pack, std::size_t len) {
    TASTY_SEAT_BODY(ConfigSaves);
    if (pack == nullptr || len == 0)
        return;
    const auto count = std::to_integer<std::uint8_t>(pack[0]);
    std::size_t off = 1;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (off >= len)
            break;
        const auto nlen = std::to_integer<std::uint8_t>(pack[off++]);
        if (off + nlen > len)
            break;
        const std::string_view name = infra::as_text(std::span(pack + off, nlen));
        off += nlen;
        std::string rel = "config/";
        rel.append(name);
        (void)intern_rel_at_(InternedFile::config_id(i), rel);
    }
    (void)push_dropping(inbox_, tally_, proto::LinkOp::BindSlotPreviews{.count = count});
}

bool ConfigSaves::bind_slot_config(std::uint8_t slot, std::string_view path) {
    TASTY_SEAT_BODY(ConfigSaves);
    proto::LinkOp::BindSlotConfig op{.slot = slot};
    const auto id = InternedFile::config_id(slot);
    if (intern_rel_at_(id, path)) {
        op.file = id;
    }
    return push_dropping(inbox_, tally_, op);
}

void ConfigSaves::publish_save_verdict_(bool ok, CorrelationTag tag) noexcept {
    (void)owner_events_.push(
        infra::make<Event>(
            Event::InfoRequest{.id = ok ? InfoId::SaveWritten : InfoId::SaveFailed},
            Event::Head{EmitSite{ERR_SITE()}, {}, tag}
        )
    );
}

void ConfigSaves::order_save_upload_() noexcept {
    (void)push_dropping(inbox_, tally_, proto::LinkOp::SaveUpload{});
}

bool ConfigSaves::order_save_ask(
    proto::SaveKind kind,
    std::uint8_t slot,
    CorrelationTag tag,
    bool gating
) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    const proto::LinkOp op = infra::make<proto::LinkOp>(
        proto::LinkOp::SaveAsk{.which = kind, .slot = slot, .tag = tag}
    );
    if (gating)
        return push_gating(inbox_, tally_, sleeper_, op);
    return push_dropping(inbox_, tally_, op);
}

bool ConfigSaves::write_durably_(std::string_view rel, std::span<const std::byte> bytes) {
    if (durable_write(vfs_, rel, bytes).has_value())
        return true;
    swf_cell_.publish(++tally_.save_write_failures);
    return false;
}

void ConfigSaves::save_config(CorrelationTag tag) {
    TASTY_SEAT_BODY(ConfigSaves);
    const PathText& stem = negotiated_.saved_cfg_stem();
    if (stem.empty()) {
        publish_save_verdict_(false, tag);
        return;
    }
    const auto sample = status_cell_.sample();
    if (sample.generation == 0) {

        publish_save_verdict_(false, tag);
        return;
    }
    std::byte bytes[16];
    for (unsigned i = 0; i < proto::StatusRegister::kWords; ++i) {
        const std::uint16_t w = sample.value.words[i];
        bytes[2 * i] = static_cast<std::byte>(w & 0xFFu);
        bytes[2 * i + 1] = static_cast<std::byte>((w >> 8) & 0xFFu);
    }
    std::string rel = "config/";
    rel.append(stem.view());
    rel.append(".CFG");
    const bool ok = write_durably_(rel, std::span<const std::byte>(bytes));

    order_save_upload_();
    publish_save_verdict_(ok, tag);
}

void ConfigSaves::on_save_bytes(const proto::LinkEvent::SaveBytes& e) {
    TASTY_SEAT_BODY(ConfigSaves);
    const proto::SaveKind kind = e.which;
    const proto::SaveStatus status = e.status;
    const CorrelationTag tag = e.tag;
    if (status != proto::SaveStatus::Bytes) {

        if (tag)
            publish_save_verdict_(false, tag);
        return;
    }
    const auto path = rx_.bytes(e.rel_path);
    const auto payload = rx_.bytes(e.payload);
    const std::string_view rel = infra::as_text(path);
    const bool ok = write_durably_(rel, std::as_bytes(payload));

    if (tag && !ok)
        publish_save_verdict_(false, tag);

    if (tag && ok && kind == proto::SaveKind::Slot) {
        ++tally_.config_saves;
        (void)order_save_ask(proto::SaveKind::SlotNames, slot_save_slot_, CorrelationTag{});
    }
}

}
