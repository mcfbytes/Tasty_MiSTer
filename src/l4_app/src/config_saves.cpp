// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/config_saves.h"

#include <algorithm>
#include <cerrno>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/config_rel.h"
#include "app/durable_write.h"
#include "app/ini_key_edit.h"
#include "app/owner_push.h"
#include "app/stream_io.h"
#include "cores/config_codec.h"
#include "cores/config_slot_count.h"
#include "cores/registry.h"
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
      owner_events_(w.owner_events.producer()),
      vfs_(w.vfs),
      status_cell_(w.status_cell),
      negotiated_(w.negotiated),
      sleeper_(w.sleeper),
      ini_(w.ini) {}

infra::OptRef<const cores::IConfigCodec> ConfigSaves::codec_() const noexcept {
    const auto row = cores::find_core(negotiated_.core_name().view());
    if (!row)
        return {};
    return row->profile.config_codec;
}

std::string ConfigSaves::slot_rel(cores::ConfigSlot slot) const {
    const auto codec = codec_();
    std::string rel;
    if (!codec)
        return rel;
    const auto name = codec->file_name(slot);
    if (name.view().empty())
        return {};
    rel = "config/";
    rel.append(name.view());
    return rel;
}

std::vector<std::uint8_t> ConfigSaves::read_slot_(std::string_view rel) const {

    if (rel.empty())
        return {};
    auto bytes = read_file(vfs_, rel);
    if (!bytes)
        return {};
    return std::move(*bytes);
}

Ex<proto::FileId> ConfigSaves::intern_value_(std::span<const std::byte> value) {
    if (value.empty())
        return proto::FileId{};
    std::vector<std::uint8_t> buf(value.size());
    std::memcpy(buf.data(), value.data(), value.size());
    return inbox_.intern_file(FileIntern{.bytes = std::move(buf)});
}

void ConfigSaves::save_dips(CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    if (!order_save_ask(proto::SaveKind::Dips, cores::ConfigSlot{}, tag)) {
        publish_save_verdict_(false, tag);
    }
}

void ConfigSaves::save_slot(cores::ConfigSlot slot, CorrelationTag tag) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    slot_save_slot_ = slot;
    if (!order_save_ask(proto::SaveKind::Slot, slot, tag)) {
        publish_save_verdict_(false, tag);
    }
}

void ConfigSaves::set_ini_key(std::string_view key, std::string_view value) {
    TASTY_SEAT_BODY(ConfigSaves);
    const svc::IniCatalog cat = ini_.sample().value;
    const std::string_view kIni = cat.active_name();

    if (kIni.empty()) {
        tally_.ini_write_failures.add();
        return;
    }
    std::string text;
    auto buf = read_file(vfs_, kIni);
    if (!buf) {

        const Error& e = buf.error();
        const bool absent = (e.code == Errc::os && e.detail == ENOENT) || e.code == Errc::not_found;
        if (!absent) {
            tally_.ini_write_failures.add();
            return;
        }
    } else {
        text.assign(infra::as_text(std::span(*buf)));
    }
    const std::string out = ini_set_key(text, key, value);
    if (!write_durably_(kIni, std::as_bytes(std::span<const char>(out.data(), out.size())))) {
        tally_.ini_write_failures.add();
    }
}

void ConfigSaves::publish_slot_previews() {
    TASTY_SEAT_BODY(ConfigSaves);
    preview_scratch_ = ConfigSlotTable{};
    const auto codec = codec_();
    if (codec) {
        const cores::ConfigSlotCount count = std::min(
            codec->slot_count(),
            cores::ConfigSlotCount::of(cores::kMaxConfigSlots)
        );
        preview_scratch_.count = count.v;
        for (std::uint8_t i = 0; i < count.v; ++i) {
            const cores::ConfigSlot slot{i};
            const std::vector<std::uint8_t> bytes = read_slot_(slot_rel(slot));
            if (!bytes.empty())
                preview_scratch_.exists = preview_scratch_.exists.with(slot);
            const auto blob = cores::ConfigBlob{
                infra::byte_cast<const std::byte>(std::span(bytes))
            };
            preview_scratch_.preview[i] = codec->preview(blob);
        }
    }
    config_slot_cell_.publish(preview_scratch_);
}

bool ConfigSaves::bind_slot_value(std::string_view rel) {
    TASTY_SEAT_BODY(ConfigSaves);
    const auto codec = codec_();
    if (!codec)
        return false;
    std::array<std::byte, cores::kMaxConfigValueBytes> value{};
    const std::vector<std::uint8_t> bytes = read_slot_(rel);
    const auto value_span = std::span<std::byte>(value).first(
        codec->value_bytes().clamp_write(0, value.size())
    );
    if (!codec->decode(
            cores::ConfigBlob{infra::byte_cast<const std::byte>(std::span(bytes))},
            value_span
        )) {
        return false;
    }
    const auto id = intern_value_(value_span);
    if (!id)
        return false;
    proto::LinkOp::BindSlotConfig op{};
    op.file = *id;
    return push_dropping(inbox_, tally_, op);
}

SlotLoad ConfigSaves::load_slot(cores::ConfigSlot slot, CorrelationTag tag) {
    TASTY_SEAT_BODY(ConfigSaves);
    const std::string rel = slot_rel(slot);
    if (rel.empty())
        return SlotLoad::NoRole;
    if (!bind_slot_value(rel)) {
        publish_save_verdict_(false, tag);
        return SlotLoad::Refused;
    }
    return SlotLoad::Bound;
}

void ConfigSaves::publish_save_verdict_(bool ok, CorrelationTag tag) noexcept {
    owner_events_.push(
        Event::InfoRequest{.id = ok ? InfoId::SaveWritten : InfoId::SaveFailed},
        Event::Head{ERR_SITE(), {}, tag}
    );
}

void ConfigSaves::order_save_upload_() noexcept {
    (void)push_dropping(inbox_, tally_, proto::LinkOp::SaveUpload{});
}

bool ConfigSaves::order_save_ask(
    proto::SaveKind kind,
    cores::ConfigSlot slot,
    CorrelationTag tag,
    bool gating
) noexcept {
    TASTY_SEAT_BODY(ConfigSaves);
    const proto::LinkOp op = infra::make<proto::LinkOp>(
        proto::LinkOp::SaveAsk{.which = kind, .slot = slot.v, .tag = tag}
    );
    if (gating)
        return push_gating(inbox_, tally_, sleeper_, op);
    return push_dropping(inbox_, tally_, op);
}

bool ConfigSaves::write_durably_(std::string_view rel, std::span<const std::byte> bytes) {
    if (durable_write(vfs_, rel, bytes).has_value())
        return true;
    tally_.save_write_failures.add();
    return false;
}

void ConfigSaves::save_config(CorrelationTag tag) {
    TASTY_SEAT_BODY(ConfigSaves);
    const auto rel = core_cfg_rel(negotiated_.saved_cfg_stem().view());
    if (!rel) {
        publish_save_verdict_(false, tag);
        return;
    }
    const auto sample = status_cell_.sample();
    if (sample.generation == 0) {

        publish_save_verdict_(false, tag);
        return;
    }

    const proto::StatusWord::Bytes bytes = sample.value.to_le_bytes();
    const bool ok = write_durably_(rel->view(), std::as_bytes(std::span{bytes}));

    order_save_upload_();
    publish_save_verdict_(ok, tag);
}

void ConfigSaves::on_save_bytes(const proto::LinkEvent::SaveBytes& e) {
    TASTY_SEAT_BODY(ConfigSaves);
    if (e.status != proto::SaveStatus::Bytes) {

        if (e.tag)
            publish_save_verdict_(false, e.tag);
        return;
    }
    if (e.which == proto::SaveKind::Slot) {
        on_slot_value(e);
        return;
    }
    const auto path = rx_.bytes(e.rel_path);
    const auto payload = rx_.bytes(e.payload);
    const bool ok = write_durably_(infra::as_text(path), std::as_bytes(payload));

    if (e.tag && !ok)
        publish_save_verdict_(false, e.tag);
}

void ConfigSaves::on_slot_value(const proto::LinkEvent::SaveBytes& e) {
    TASTY_SEAT_BODY(ConfigSaves);
    const auto codec = codec_();
    const std::string rel = codec ? slot_rel(slot_save_slot_) : std::string{};
    const auto payload = rx_.bytes(e.payload);
    std::array<std::byte, cores::kMaxConfigValueBytes> file{};
    const auto encoded = codec ? codec->encode(infra::byte_cast<const std::byte>(payload), file)
                               : std::span<const std::byte>{};
    const bool ok = !rel.empty() && !encoded.empty() && write_durably_(rel, encoded);
    if (e.tag && !ok)
        publish_save_verdict_(false, e.tag);
    if (e.tag && ok) {
        ++tally_.config_saves;

        publish_slot_previews();
    }
}

}
