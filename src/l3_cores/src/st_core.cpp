// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_core.h"
#include "cores/core_adopt.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "cores/disk_port.h"
#include "cores/st_media_fact.h"
#include "cores/st_option_row_index.h"
#include "infra/indexed_array.h"
#include "infra/indexed_span.h"
#include "proto/link.h"
#include "proto/spi_frame.h"
#include "proto/download_session.h"
#include "proto/image_bracket.h"
#include "proto/uio_opcodes.h"

namespace mister::cores {

namespace {

constexpr auto kBlankCart = [] {
    std::array<std::uint8_t, StCore::kCartBytes> a{};
    a.fill(std::uint8_t{0xFF});
    return a;
}();

constexpr auto kDiskSlots = std::array{
    st_ctrl::StSlot::FloppyA,
    st_ctrl::StSlot::FloppyB,
    st_ctrl::StSlot::Acsi0,
    st_ctrl::StSlot::Acsi1
};

[[nodiscard]] constexpr proto::DiskUnit unit_of(AcsiTarget target) noexcept {
    return proto::DiskUnit{target.v};
}

}

StCore::StCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

Ex<void> StCore::push_control_() {
    proto::SpiFrame frame{host().link};
    if (auto r = frame.begin(proto::UioOp::SetStatus2); !r)
        return r;
    const std::uint16_t payload[4] = {
        static_cast<std::uint16_t>(cfg_.system_ctrl),
        static_cast<std::uint16_t>((cfg_.system_ctrl >> 16)),
        static_cast<std::uint16_t>(cfg_.ext_ctrl),
        static_cast<std::uint16_t>((cfg_.ext_ctrl >> 16)),
    };
    for (const std::uint16_t w : payload) {
        if (auto r = frame.word(w); !r)
            return r;
    }
    if (auto r = frame.end(); !r)
        return r;
    ++control_pushes_;
    return {};
}

Ex<void> StCore::update_sysctrl_(st_ctrl::CtrlWord ctrl) {
    store_sysctrl_(ctrl);
    return push_control_();
}

void StCore::store_sysctrl_(st_ctrl::CtrlWord ctrl) noexcept {
    cfg_.system_ctrl = ctrl.v;
}

Ex<void> StCore::set_system_ctrl(std::uint32_t ctrl) {
    return update_sysctrl_(st_ctrl::CtrlWord{ctrl});
}

Ex<void> StCore::set_ext_ctrl(std::uint32_t ext) {
    cfg_.ext_ctrl = ext;
    return push_control_();
}

Ex<void> StCore::do_adopt(const CoreAdopt&) {
    return {};
}

Ex<void> StCore::eject_all_() {
    for (const st_ctrl::StSlot slot : kDiskSlots) {
        if (auto r = insert_disk_(slot, {}); !r)
            return r;
    }
    return {};
}

Ex<void> StCore::on_apply_load_facts(std::span<const std::uint8_t> facts) {
    const auto f = StMediaFact::of(facts);
    if (!f) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(facts.size())}
        );
    }
    switch (f->verb) {
        case StMediaFact::Verb::Hold:

            if (host().disk)
                host().disk->abandon_command();
            if (auto r = update_sysctrl_(
                    st_ctrl::CtrlWord{cfg_.system_ctrl}.with(st_ctrl::CtrlWord::kCpuReset)
                );
                !r) {
                return r;
            }
            if (!f->cold) {
                ++warm_resets_;
                return {};
            }
            ++cold_resets_;
            return clear_low_ram_();
        case StMediaFact::Verb::NoTos:
            ++tos_refusals_;
            return {};
        case StMediaFact::Verb::NoCart:
            return tx_cart_(kBlankCart);
        case StMediaFact::Verb::Release:
            return update_sysctrl_(
                st_ctrl::CtrlWord{cfg_.system_ctrl}.without(st_ctrl::CtrlWord::kCpuReset)
            );
        case StMediaFact::Verb::kCount:
            break;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(f->verb)}
    );
}

Ex<void> StCore::clear_low_ram_() {

    static constexpr std::array<std::uint8_t, kClearBytes> kZero{};
    auto ds = proto::DownloadSession::begin(host().link, kClearIndex);
    if (!ds)
        return std::unexpected(ds.error());
    if (auto r = ds->write(std::span<const std::uint8_t>(kZero), hal::Width::Word); !r) {
        return r;
    }
    return ds->end();
}

Ex<void> StCore::tx_tos_(std::span<const std::uint8_t> bytes, std::string_view ext) {
    const std::uint64_t len = bytes.size();
    proto::WideIoIndex index{};
    if (len >= kTosLargeBytes) {
        index = kTosLargeIndex;
    } else if (len == kTosSmallBytes) {
        index = kTosSmallIndex;
    } else {
        ++tos_refusals_;
        return {};
    }
    proto::SessionParams params{};
    params.ext = ext;
    auto bracket = proto::ImageBracket::open(image_sink(), index, params);
    if (!bracket)
        return std::unexpected(bracket.error());

    for (std::size_t off = 0; off < bytes.size(); off += kChunkBytes) {
        const std::size_t n = std::min(kChunkBytes, bytes.size() - off);
        if (auto r = bracket->write(bytes.subspan(off, n)); !r)
            return r;
    }
    if (auto r = bracket->end(); !r)
        return r;
    bytes_sent_ = len;
    return {};
}

Ex<void> StCore::tx_cart_from_(std::span<const std::uint8_t> image) {
    if (st_ctrl::CtrlWord{cfg_.system_ctrl}.has(st_ctrl::CtrlWord::kDongle)) {
        return tx_cart_(kBlankCart);
    }
    if (image.size() != kCartBufferBytes) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(image.size())}
        );
    }

    return tx_cart_(image.subspan(4));
}

Ex<void> StCore::tx_cart_(std::span<const std::uint8_t> rom) {
    auto bracket = proto::ImageBracket::open(image_sink(), kCartIndex);
    if (!bracket)
        return std::unexpected(bracket.error());
    if (auto r = bracket->write(rom); !r)
        return r;
    bytes_sent_ = kCartBytes;
    return bracket->end();
}

Ex<void> StCore::insert_disk_(st_ctrl::StSlot slot, std::string_view path) {
    if (const auto target = slot.acsi_target()) {

        if (!path.empty())
            return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
        return eject_hdd_(*target);
    }
    if (!slot.is_floppy())
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    FixedStr<kPathBytes, StrFit::Clip>& img = infra::at_index(floppy_img_, slot);
    (void)img.assign(path);

    const st_ctrl::CtrlWord sc{cfg_.system_ctrl};
    const st_ctrl::CtrlWord::Bit bit = st_ctrl::write_protect_bit(slot);
    store_sysctrl_(img.empty() ? sc.with(bit) : sc.without(bit));
    return push_control_();
}

Ex<void> StCore::eject_hdd_(AcsiTarget target) {
    if (host().disk)
        host().disk->abandon_command_on(unit_of(target));
    (void)cfg_.acsi_img[target].assign({});
    store_sysctrl_(st_ctrl::CtrlWord{cfg_.system_ctrl}.without(st_ctrl::acsi_enable_bit(target)));
    acsi_[target] = AcsiBind{};
    auto pushed = push_control_();
    if (host().disk)
        host().disk->order_bind(unit_of(target), {});
    return pushed;
}

MountState StCore::mount_slot(proto::SlotIndex slot, std::string_view path) {
    TASTY_SEAT_BODY(StCore);
    const auto disk_slot = st_ctrl::StSlot::of(IoIndex{slot.v});
    if (!disk_slot)
        return MountState::Failed;
    const auto target = disk_slot->acsi_target();
    if (!target)
        return MountState::Failed;
    if (!host().disk)
        return MountState::Failed;
    IDiskPort& disk = *host().disk;
    if (!acsi_[*target].out || cfg_.acsi_img[*target].view() != path) {
        (void)cfg_.acsi_img[*target].assign(path);
        acsi_[*target] = AcsiBind{.bound = false, .out = true};
        disk.order_bind(unit_of(*target), path);
    }
    const MountState answer = disk.bind_answer(unit_of(*target));
    if (answer == MountState::Pending)
        return MountState::Pending;
    acsi_[*target].out = false;
    const st_ctrl::CtrlWord::Bit enable = st_ctrl::acsi_enable_bit(*target);
    acsi_[*target].bound = answer == MountState::Done && !path.empty();
    const st_ctrl::CtrlWord sc{cfg_.system_ctrl};
    store_sysctrl_(acsi_[*target].bound ? sc.with(enable) : sc.without(enable));
    if (auto r = push_control_(); !r)
        return MountState::Failed;
    return answer;
}

Ex<void> StCore::on_mount(IoIndex slot, const MountedPath& p) {
    const auto disk_slot = st_ctrl::StSlot::of(slot);
    if (!disk_slot)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    if (disk_slot->is_disk())
        return insert_disk_(*disk_slot, p.display);
    if (*disk_slot == st_ctrl::StSlot::Tos) {

        (void)cfg_.tos_img.assign(p.display);
        return {};
    }
    if (*disk_slot == st_ctrl::StSlot::Cart) {

        (void)cart_img_.assign(p.display);
        return {};
    }
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
}

void StCore::on_set_pending_file_ext(std::string_view ext) noexcept {
    (void)ext_.assign(ext);
}

Ex<void> StCore::on_file_tx(IoIndex index, std::span<const std::uint8_t> bytes) {
    bytes_sent_ = 0;
    if (index == kTosSlot)
        return tx_tos_(bytes, ext_.view());
    if (index == kCartSlot)
        return tx_cart_from_(bytes);
    return unimplemented(ERR_SITE());
}

FixedStr<kConfigNameChars, StrFit::Clip> StCore::file_name(ConfigSlot slot) const noexcept {
    TASTY_SEAT_BODY(StCore);
    FixedStr<kConfigNameChars, StrFit::Clip> name{};
    if (slot.v >= st_cfg::kSlotCount)
        return name;
    char buf[kConfigNameChars]{};
    (void)std::snprintf(
        buf,
        sizeof(buf),
        "%s%c%s",
        st_cfg::kFileStem,
        static_cast<char>('0' + slot.v),
        st_cfg::kFileExt
    );
    (void)name.assign(buf);
    return name;
}

std::span<const std::byte> StCore::snapshot() noexcept {
    TASTY_SEAT_BODY(StCore);
    cfg_.encode(image_, cart_img_.view());
    return image_;
}

Ex<void> StCore::restore(ConfigBlob blob) {
    TASTY_SEAT_BODY(StCore);

    if (auto r = eject_all_(); !r)
        return r;
    cfg_ = StConfig::defaults();
    cfg_.overlay(blob.bytes, cart_img_.view(), image_);

    if (const auto cart = StConfig::cart_of(image_); !cart.empty()) {
        (void)cart_img_.assign(cart);
    }
    return {};
}

FixedStr<kConfigPreviewChars, StrFit::Clip> StCore::preview(ConfigBlob blob) const noexcept {
    TASTY_SEAT_BODY(StCore);
    return StConfig::preview(blob);
}

std::uint32_t StCore::word_(WordId w) const noexcept {
    return w == WordId{0} ? cfg_.system_ctrl : cfg_.ext_ctrl;
}

Ex<void> StCore::push_word_(WordId w, std::uint32_t v) {
    return w == WordId{0} ? update_sysctrl_(st_ctrl::CtrlWord{v}) : set_ext_ctrl(v);
}

OptionRowView StCore::option_view(proto::OptionRowIndex index) noexcept {
    TASTY_SEAT_BODY(StCore);
    const infra::IndexedSpan<const OptionRow, StRowPos> rows{profile().option_rows};
    const auto found = rows.find(row_pos(index));
    if (!found)
        return {};
    const OptionRow& r = *found;
    switch (r.kind) {
        case OptionRow::Kind::Cycle: {
            if (!r.bits.valid()) {
                const auto o = option_of(r.id);
                if (!o)
                    return {};
                return StOptionRow::of(*o).cycle_view(facts_(), r);
            }
            const auto cur = static_cast<std::uint8_t>(
                (word_(r.word) >> r.bits.start.v) & r.bits.mask()
            );
            return {cur, false, cur < r.values.size() ? r.values[cur] : std::string_view{}};
        }
        case OptionRow::Kind::File: {
            const auto o = option_of(r.id);
            if (!o)
                return {};
            return StOptionRow::of(*o).file_view(facts_(), view_text_);
        }
        case OptionRow::Kind::Action:
        case OptionRow::Kind::Link:
        case OptionRow::Kind::Host:
        case OptionRow::Kind::Separator:
        case OptionRow::Kind::kCount:
            return {};
    }
    return {};
}

StOptionRow::Facts StCore::facts_() const noexcept {
    return {
        .system_ctrl = st_ctrl::CtrlWord{cfg_.system_ctrl},
        .ext_ctrl = cfg_.ext_ctrl,
        .floppy =
            {infra::at_index(floppy_img_, st_ctrl::StSlot::FloppyA).view(),
             infra::at_index(floppy_img_, st_ctrl::StSlot::FloppyB).view()},
        .hdd_bound = {acsi_[AcsiTarget{0}].bound, acsi_[AcsiTarget{1}].bound},
        .acsi = {cfg_.acsi_img[AcsiTarget{0}].view(), cfg_.acsi_img[AcsiTarget{1}].view()},
        .tos = cfg_.tos_img.view(),
        .cart = cart_img_.view()
    };
}

Ex<void> StCore::set_option(proto::OptionRowIndex index, proto::FieldValue choice) {
    TASTY_SEAT_BODY(StCore);
    const infra::IndexedSpan<const OptionRow, StRowPos> rows{profile().option_rows};
    const auto found = rows.find(row_pos(index));
    if (!found) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), index.v});
    }
    const OptionRow& r = *found;
    const auto o = option_of(r.id);
    if (!o)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), r.id});
    switch (r.kind) {
        case OptionRow::Kind::Cycle: {
            const std::uint32_t pick = choice.v;
            if (pick >= r.choice_count()) {
                return std::unexpected(Error{Errc::slot_range, ERR_SITE(), (pick << 8) | index.v});
            }
            if (!r.bits.valid()) {
                const auto w = StOptionRow::of(*o).cycle_set(
                    facts_(),
                    static_cast<std::uint8_t>(pick)
                );
                if (!w)
                    return std::unexpected(w.error());
                return apply_write_(*w);
            }
            const std::uint32_t field = r.bits.mask() << r.bits.start.v;
            const std::uint32_t v = (word_(r.word) & ~field) | ((pick << r.bits.start.v) & field);
            return push_word_(r.word, v);
        }
        case OptionRow::Kind::Action:
        case OptionRow::Kind::File:
        case OptionRow::Kind::Link:
        case OptionRow::Kind::Host:
        case OptionRow::Kind::Separator:
        case OptionRow::Kind::kCount:
            break;
    }
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), index.v});
}

Ex<void> StCore::apply_write_(const StOptionRow::Write& w) {
    if (w.system_ctrl) {
        if (auto r = update_sysctrl_(*w.system_ctrl); !r)
            return r;
    }
    if (w.ext_ctrl)
        return set_ext_ctrl(*w.ext_ctrl);
    return {};
}

void StCore::service_kick() noexcept {
    TASTY_SEAT_BODY(StCore);
    if (host().disk) {
        proto::Link link{host().link};
        host().disk->service_rt(link, kAcsiPoll);
    }
}

bool StCore::acsi_bound(AcsiTarget target) const noexcept {
    return target.v < kAcsiTargets && acsi_[target].bound;
}

std::string_view StCore::acsi_path(AcsiTarget target) const noexcept {
    return target.v < kAcsiTargets ? cfg_.acsi_img[target].view() : std::string_view{};
}

}
