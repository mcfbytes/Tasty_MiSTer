// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/page_stack.h"

namespace mister::ui {

void PageStack::push(std::unique_ptr<Page> p) {
    if (p == nullptr)
        return;
    pages_.push_back(static_cast<std::unique_ptr<Page>&&>(p));
}

void PageStack::pop() {
    if (pages_.empty())
        return;
    retired_refusals_ += pages_.back()->refusals();
    pages_.pop_back();
}

void PageStack::replace(std::unique_ptr<Page> p) {
    if (!pages_.empty()) {
        retired_refusals_ += pages_.back()->refusals();
        pages_.pop_back();
    }
    push(static_cast<std::unique_ptr<Page>&&>(p));
}

void PageStack::clear() noexcept {
    for (const std::unique_ptr<Page>& p : pages_)
        retired_refusals_ += p->refusals();
    pages_.clear();
}

std::uint32_t PageStack::page_refusals() const noexcept {
    std::uint32_t n = retired_refusals_;
    for (const std::unique_ptr<Page>& p : pages_)
        n += p->refusals();
    return n;
}

bool PageStack::any_bt_pairing() const noexcept {
    for (const std::unique_ptr<Page>& p : pages_) {
        if (p->bt_pairing_active())
            return true;
    }
    return false;
}

bool PageStack::any_runs_a_process() const noexcept {
    for (const std::unique_ptr<Page>& p : pages_) {
        if (p->runs_a_process())
            return true;
    }
    return false;
}

bool PageStack::any_takes_focus() const noexcept {
    return focus_top().has_value();
}

infra::OptRef<const Page> PageStack::focus_top() const noexcept {
    for (auto it = pages_.rbegin(); it != pages_.rend(); ++it) {
        if ((*it)->facts().takes_input_focus)
            return infra::opt_ref_of<const Page>(it->get());
    }
    return {};
}

}
