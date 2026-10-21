// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::ui {

template <class T>
RowCount ListView<T>::page_step() const noexcept {

    if (height_.v == 0)
        return RowCount{1};
    if (policy_.browse_expand && height_.v > 1)
        return RowCount{height_.v - 1u};
    return height_;
}

template <class T>
void ListView<T>::recentre() noexcept {
    view_ = Viewport::centre(
        view_.selected,
        RowCount{static_cast<std::uint32_t>(items_.size())},
        height_,
        policy_
    );
}

template <class T>
void ListView<T>::select(RowIndex i) noexcept {
    const auto n = static_cast<std::uint32_t>(items_.size());
    view_.selected = RowIndex{n == 0 ? 0u : (i.v >= n ? n - 1u : i.v)};
    recentre();
}

template <class T>
void ListView<T>::next() noexcept {
    if (items_.empty())
        return;
    const auto last = static_cast<std::uint32_t>(items_.size() - 1);
    view_.selected = (view_.selected.v >= last) ? RowIndex{0} : RowIndex{view_.selected.v + 1u};
    recentre();
}

template <class T>
void ListView<T>::prev() noexcept {
    if (items_.empty())
        return;
    if (view_.selected.v == 0) {
        end();
        return;
    }
    view_.selected = RowIndex{view_.selected.v - 1u};
    recentre();
}

template <class T>
void ListView<T>::next_page() noexcept {
    if (items_.empty())
        return;
    const auto last = static_cast<std::uint32_t>(items_.size() - 1);
    const std::uint32_t step = page_step().v;
    view_.selected = (last - view_.selected.v < step) ? RowIndex{last}
                                                      : RowIndex{view_.selected.v + step};
    recentre();
}

template <class T>
void ListView<T>::prev_page() noexcept {
    if (items_.empty())
        return;
    const std::uint32_t step = page_step().v;
    view_.selected = (view_.selected.v < step) ? RowIndex{0} : RowIndex{view_.selected.v - step};
    recentre();
}

template <class T>
void ListView<T>::home() noexcept {
    if (items_.empty())
        return;
    view_.selected = RowIndex{0};
    recentre();
}

template <class T>
void ListView<T>::end() noexcept {
    if (items_.empty())
        return;
    view_.selected = RowIndex{static_cast<std::uint32_t>(items_.size() - 1)};
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
