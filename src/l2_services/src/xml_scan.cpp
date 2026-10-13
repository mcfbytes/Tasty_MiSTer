// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/xml_scan.h"

#include "infra/ascii.h"
#include "svc/xml_scanner.h"

namespace mister::svc::xml {

std::string_view rbf_text(std::string_view xml) noexcept {
    std::string_view out;
    bool inside = false;
    XmlScanner scan{xml};
    Attr attrs[kMaxAttrs];
    while (const auto t = scan.next_token(attrs)) {
        switch (t->kind) {
            case Token::Kind::Text:

                if (inside) {
                    inside = false;
                    out = t->text;
                }
                break;
            case Token::Kind::Start:

                if (infra::ieq(t->tag.name, "rbf")) inside = true;
                if (t->tag.self_closing) inside = false;
                break;
            case Token::Kind::End:
            case Token::Kind::Markup:

                inside = false;
                break;
        }
    }

    while (!out.empty() && is_space(out.front()))
        out.remove_prefix(1);
    while (!out.empty() && is_space(out.back()))
        out.remove_suffix(1);
    return out;
}

}  // namespace mister::svc::xml
