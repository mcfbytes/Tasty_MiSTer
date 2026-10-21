// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_core.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "cores/disk_port.h"
#include "cores/st_media_fact.h"
#include "hal/selected.h"
#include "proto/download_session.h"
#include "proto/image_bracket.h"

namespace mister::cores {

namespace {

constexpr hal::SpiWord kSetStatus2{0x1E};

constexpr auto kBlankCart = [] {
    std::array<std::uint8_t, StCore::kCartBytes> a{};
    a.fill(std::uint8_t{0xFF});
    return a;
}();

}

StCore::StCore(const CoreProfile& p, const HostServices& h) : Core(p, h) {}

Ex<void> StCore::push_control_() {
    hal::Selected cs(host().link, hal::ChipSelect::Io);
    if (auto r = host().link.transfer(kSetStatus2); !r)
        return std::unexpected(r.error());
    const std::uint16_t payload[4] = {
        static_cast<std::uint16_t>(cfg_.system_ctrl & 0xFFFFu),
        static_cast<std::uint16_t>((cfg_.system_ctrl >> 16) & 0xFFFFu),
        static_cast<std::uint16_t>(cfg_.ext_ctrl & 0xFFFFu),
        static_cast<std::uint16_t>((cfg_.ext_ctrl >> 16) & 0xFFFFu),
    };
    for (const std::uint16_t w : payload) {
        if (auto r = host().link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    ++control_pushes_;
    return {};
}

Ex<void> StCore::update_sysctrl_(std::uint32_t ctrl) {
    cfg_.system_ctrl = ctrl;
    return push_control_();
}

Ex<void> StCore::set_system_ctrl(std::uint32_t ctrl) {
    return update_sysctrl_(ctrl);
}

Ex<void> StCore::set_ext_ctrl(std::uint32_t ext) {
    cfg_.ext_ctrl = ext;
    return push_control_();
}

Ex<void> StCore::do_adopt(const CoreAdopt&) {
    return {};
}

Ex<void> StCore::eject_all_() {
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        if (auto r = insert_disk_(IoIndex{slot}, {}); !r)
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
            if (auto r = update_sysctrl_(cfg_.system_ctrl | st_ctrl::kCpuReset); !r)
                return r;
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
            return update_sysctrl_(cfg_.system_ctrl & ~st_ctrl::kCpuReset);
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
    if ((cfg_.system_ctrl & st_ctrl::kDongle) != 0)
        return tx_cart_(kBlankCart);
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

Ex<void> StCore::insert_disk_(IoIndex slot, std::string_view path) {
    if (slot.v <= 1) {
        floppy_inserted_[slot.v] = !path.empty();
        (void)floppy_img_[slot.v].assign(path);
        const std::uint32_t bit = st_ctrl::write_protect_bit(slot.v);

        if (floppy_inserted_[slot.v]) {
            cfg_.system_ctrl &= ~bit;
        } else {
            cfg_.system_ctrl |= bit;
        }
        return push_control_();
    }

    if (!path.empty())
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), slot.v});
    return eject_hdd_(static_cast<std::uint8_t>(slot.v & 1u));
}

Ex<void> StCore::eject_hdd_(std::uint8_t target) {
    if (host().disk)
        host().disk->abandon_command_on(target);
    (void)cfg_.acsi_img[target].assign({});
    cfg_.system_ctrl &= ~(st_ctrl::kAcsi0Enable << target);
    hdd_bound_[target] = false;
    bind_out_[target] = false;
    auto pushed = push_control_();
    if (host().disk)
        host().disk->order_bind(target, {});
    return pushed;
}

MountState StCore::mount_slot(proto::SlotIndex slot, std::string_view path) {
    TASTY_SEAT_BODY(StCore);
    if (slot.v != kAcsiSlot0.v && slot.v != kAcsiSlot1.v)
        return MountState::Failed;
    const auto target = static_cast<std::uint8_t>(slot.v & 1u);
    if (!host().disk)
        return MountState::Failed;
    IDiskPort& disk = *host().disk;
    if (!bind_out_[target] || cfg_.acsi_img[target].view() != path) {
        (void)cfg_.acsi_img[target].assign(path);
        hdd_bound_[target] = false;
        bind_out_[target] = true;
        disk.order_bind(target, path);
    }
    const MountState answer = disk.bind_answer(target);
    if (answer == MountState::Pending)
        return MountState::Pending;
    bind_out_[target] = false;
    const std::uint32_t enable = st_ctrl::kAcsi0Enable << target;
    hdd_bound_[target] = answer == MountState::Done && !path.empty();
    if (hdd_bound_[target]) {
        cfg_.system_ctrl |= enable;
    } else {
        cfg_.system_ctrl &= ~enable;
    }
    if (auto r = push_control_(); !r)
        return MountState::Failed;
    return answer;
}

Ex<void> StCore::on_mount(IoIndex slot, const MountedPath& p) {
    if (slot.v <= 3)
        return insert_disk_(slot, p.display);
    if (slot == kTosSlot) {

        (void)cfg_.tos_img.assign(p.display);
        return {};
    }
    if (slot == kCartSlot) {

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
    return w == WordId{0} ? update_sysctrl_(v) : set_ext_ctrl(v);
}

OptionRowView StCore::option_view(std::size_t row) noexcept {
    TASTY_SEAT_BODY(StCore);
    const auto rows = profile().option_rows;
    if (row >= rows.size())
        return {};
    const OptionRow& r = rows[row];
    switch (r.kind) {
        case OptionRow::Kind::Cycle: {
            if (!r.bits.valid()) {
                return StOptionRow::of(static_cast<StOption>(r.id)).cycle_view(facts_(), r);
            }
            const auto cur = static_cast<std::uint8_t>(
                (word_(r.word) >> r.bits.start.v) & r.bits.mask()
            );
            return {cur, false, cur < r.values.size() ? r.values[cur] : std::string_view{}};
        }
        case OptionRow::Kind::File:
            return StOptionRow::of(static_cast<StOption>(r.id)).file_view(facts_(), view_text_);
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
        .system_ctrl = cfg_.system_ctrl,
        .ext_ctrl = cfg_.ext_ctrl,
        .floppy_in = floppy_inserted_,
        .floppy = {floppy_img_[0].view(), floppy_img_[1].view()},
        .hdd_bound = hdd_bound_,
        .acsi = {cfg_.acsi_img[0].view(), cfg_.acsi_img[1].view()},
        .tos = cfg_.tos_img.view(),
        .cart = cart_img_.view()
    };
}

Ex<void> StCore::set_option(std::size_t row, std::uint8_t choice) {
    TASTY_SEAT_BODY(StCore);
    const auto rows = profile().option_rows;
    if (row >= rows.size()) {
        return std::unexpected(
            Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(row)}
        );
    }
    const OptionRow& r = rows[row];
    const auto o = static_cast<StOption>(r.id);
    switch (r.kind) {
        case OptionRow::Kind::Cycle: {
            if (choice >= r.choice_count()) {
                return std::unexpected(
                    Error{
                        Errc::slot_range,
                        ERR_SITE(),
                        (static_cast<std::uint32_t>(choice) << 8) | static_cast<std::uint32_t>(row)
                    }
                );
            }
            if (!r.bits.valid()) {
                const auto w = StOptionRow::of(o).cycle_set(facts_(), choice);
                if (!w)
                    return std::unexpected(w.error());
                return apply_write_(*w);
            }
            const std::uint32_t field = r.bits.mask() << r.bits.start.v;
            const std::uint32_t v = (word_(r.word) & ~field)
                | ((static_cast<std::uint32_t>(choice) << r.bits.start.v) & field);
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
    return std::unexpected(Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(row)});
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
    if (host().disk)
        host().disk->service_rt(host().link, kAcsiPoll);
}

bool StCore::acsi_bound(std::uint8_t target) const noexcept {
    return target < kAcsiTargets && hdd_bound_[target];
}

std::string_view StCore::acsi_path(std::uint8_t target) const noexcept {
    return target < kAcsiTargets ? cfg_.acsi_img[target].view() : std::string_view{};
}

}
