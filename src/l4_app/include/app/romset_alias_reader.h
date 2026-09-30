// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "app/romset_aliases.h"
#include "cores/romset_files.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}
namespace mister::cores {
struct FileSlot;
class IRomsetAliasSource;
}  // namespace mister::cores

namespace mister::app {

class RomsetAliasReader final : public IRomsetAliases, private cores::IRomsetFiles {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit RomsetAliasReader(const svc::Vfs& vfs) noexcept;
    ~RomsetAliasReader() override;
    RomsetAliasReader(const RomsetAliasReader&) = delete;
    RomsetAliasReader& operator=(const RomsetAliasReader&) = delete;

    void bind(const cores::FileSlot& slot);

    [[nodiscard]] RomsetAlias query(std::string_view dir, std::string_view home,
                                    std::string_view entry, std::string_view key) override;

    bool bound() const noexcept { return source_ != nullptr; }

private:
    std::optional<std::string> read_prefix(std::string_view path, std::uint64_t cap) override;

    const svc::Vfs* vfs_;
    std::unique_ptr<cores::IRomsetAliasSource> source_;
};

}  // namespace mister::app
