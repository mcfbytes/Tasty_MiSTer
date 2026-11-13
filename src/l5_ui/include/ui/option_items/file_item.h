// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>

#include "ui/option_item.h"

namespace mister::ui {

class FileItem final : public OptionItem {
public:
    FileItem() noexcept : OptionItem(Kind::File) {}

    [[nodiscard]] std::optional<proto::FileRowId> file_row() const noexcept override {
        return proto::FileRowId::file(ioctl_index);
    }

private:
    BrowserSession::Request on_shape_browse(
        BrowserSession::Request req,
        const BrowseFacts& f
    ) const override;
    Carry on_build(const Source& in, const BuildEnv& env) override;
    std::string on_compose(const Look& look) const override;
    std::optional<app::PageDescription::Row> on_describe(const Look& look) const override;
    Transition on_select(IOptionHost& host) const override;
    Transition on_step(CycleDir dir, proto::FieldValue value, IOptionHost& host) const override;
};

}
