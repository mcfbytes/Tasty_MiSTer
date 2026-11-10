// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::ui {

template <class T>
RowCount ListView<T>::page_step() const noexcept {

    if (height_.empty())
        return kOneRow;
    if (policy_.browse_expand && height_ > kOneRow)
        return height_ - kOneRow;
    return height_;
}

template <class T>
void ListView<T>::recentre() noexcept {
    view_ = Viewport::centre(
        view_.selected,
        ListWindow{.items = items_.size(), .height = height_},
        policy_
    );
}

template <class T>
void ListView<T>::select(RowIndex i) noexcept {
    const RowCount n = items_.size();
    view_.selected = n.empty() ? RowIndex{} : (i >= row_after(n) ? last_row(n) : i);
    recentre();
}

template <class T>
template <class Pred>
bool ListView<T>::select_next(Pred&& pred, Wrap wrap) noexcept {
    const RowIndex end = row_after(items_.size());
    for (RowIndex i = view_.selected + kOneRow; i < end; i += kOneRow) {
        if (pred(items_[i])) {
            select(i);
            return true;
        }
    }
    if (wrap == Wrap::No)
        return false;
    for (const RowIndex i : rows(items_.size())) {
        if (pred(items_[i])) {
            select(i);
            return true;
        }
    }
    return false;
}

template <class T>
void ListView<T>::next() noexcept {
    if (items_.empty())
        return;
    const RowIndex last = last_row(items_.size());
    view_.selected = (view_.selected >= last) ? RowIndex{} : view_.selected + kOneRow;
    recentre();
}

template <class T>
void ListView<T>::prev() noexcept {
    if (items_.empty())
        return;
    if (view_.selected == RowIndex{}) {
        end();
        return;
    }
    view_.selected = view_.selected - kOneRow;
    recentre();
}

template <class T>
void ListView<T>::next_page() noexcept {
    if (items_.empty())
        return;
    const RowIndex last = last_row(items_.size());
    const RowCount step = page_step();
    view_.selected = (last - view_.selected < step) ? last : view_.selected + step;
    recentre();
}

template <class T>
void ListView<T>::prev_page() noexcept {
    if (items_.empty())
        return;
    const RowCount step = page_step();
    view_.selected = (view_.selected.before() < step) ? RowIndex{} : view_.selected - step;
    recentre();
}

template <class T>
void ListView<T>::home() noexcept {
    if (items_.empty())
        return;
    view_.selected = RowIndex{};
    recentre();
}

template <class T>
void ListView<T>::end() noexcept {
    if (items_.empty())
        return;
    view_.selected = last_row(items_.size());
    recentre();
}

template <class T>
void ListView<T>::next_group() noexcept {}

template <class T>
void ListView<T>::prev_group() noexcept {}

template <class T>
void ListView<T>::seek_letter(char) noexcept {}

template <class T>
void ListView<T>::select_named(std::string_view) noexcept {}

}
