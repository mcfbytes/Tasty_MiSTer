// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/atari8_loader.h"

#include <utility>
#include <vector>

#include "cores/atari8/cart_image.h"
#include "cores/atari8/cart_layout.h"
#include "cores/atari8/cart_matches.h"
#include "cores/atari8/cart_rows.h"
#include "cores/atari8/cart_slot.h"
#include "cores/held_image_file.h"
#include "infra/byte_view.h"
#include "svc/read_bounded.h"

namespace mister::cores::atari8 {

namespace {

constexpr proto::FileSize kStackableFirstCart{0x10'0000};

constexpr proto::FileSize kCorinaEepromSpace{0x8'0000};

[[nodiscard]] proto::FileSize image_bytes(const CartMatch& m, CartForm form, proto::FileSize size) {
    const proto::FileSize body = form == CartForm::Car ? size.after_header(kCarHeaderBytes) : size;
    return m.layout == CartLayout::Corina ? body + kCorinaEepromSpace : body;
}

}

Ex<LoadPlan> Atari8Loader::plan(const LoadAsk& ask, const proto::StatusWord&) {
    TASTY_SEAT_BODY(Atari8Loader);
    planned_.reset();
    first_cart_.reset();
    LoadPlan plan{};
    plan.count = 1;
    const std::optional<CartSlot> slot = cart_slot_of(machine_, ask.index.io());
    if (!slot)
        return plan;
    if (*slot == CartSlot::Second && memo_.first_cart > kStackableFirstCart)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    form_ = cart_form_of(ask.index);
    std::vector<std::uint8_t> head = (form_ == CartForm::Car ? kCarHeadBytes : kRawHeadBytes)
                                         .min(ask.size)
                                         .zeroed<std::vector<std::uint8_t>>();
    auto file = vfs_.open(ask.path, svc::OpenMode::Read);
    if (!file)
        return std::unexpected(file.error());
    if (auto r = (*file)->read_exact_at(proto::kFileStart, std::as_writable_bytes(std::span(head)));
        !r)
        return std::unexpected(r.error());
    const auto match = match_carts(cart_table(machine_), form_, head, ask.size).pick(ask.choice);
    if (!match)
        return std::unexpected(match.error());
    planned_ = *match;
    if (*slot == CartSlot::First && machine_ == Atari8Machine::Computer)
        first_cart_ = image_bytes(*match, form_, ask.size);
    plan.rows[0].derived = true;
    return plan;
}

Ex<std::unique_ptr<svc::IFile>> Atari8Loader::source(
    const LoadAsk& ask,
    std::unique_ptr<svc::IFile> raw
) {
    TASTY_SEAT_BODY(Atari8Loader);
    if (!planned_ || raw == nullptr)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    auto bytes = svc::read_bounded(*raw, ask.size.file_word<std::uint64_t>());
    if (!bytes)
        return std::unexpected(bytes.error());
    return std::make_unique<HeldImageFile>(
        lay_out_cart(*planned_, form_, infra::byte_cast<const std::uint8_t>(std::span(*bytes)))
    );
}

Ex<void> Atari8Loader::finish(infra::OptRef<ICoreWindow>) {
    TASTY_SEAT_BODY(Atari8Loader);
    if (first_cart_)
        memo_.first_cart = *std::exchange(first_cart_, std::nullopt);
    return {};
}

std::span<const std::uint8_t> Atari8Loader::facts() const noexcept {
    TASTY_SEAT_BODY(Atari8Loader);
    return {};
}

}
