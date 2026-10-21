// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "svc/xml_attr.h"
#include "svc/xml_scan.h"
#include "svc/xml_tag.h"
#include "svc/xml_token.h"

namespace mister::svc::xml {

class XmlScanner {
public:
    explicit XmlScanner(std::string_view xml) noexcept : xml_(xml) {}

    [[nodiscard]] std::optional<Token> next_token(std::span<Attr, kMaxAttrs> attrs) noexcept;

    [[nodiscard]] std::optional<Tag> next_tag(std::span<Attr, kMaxAttrs> attrs) noexcept;

    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }

private:
    [[nodiscard]] bool skip_markup() noexcept;

    std::string_view xml_;
    std::size_t pos_ = 0;
};

}
