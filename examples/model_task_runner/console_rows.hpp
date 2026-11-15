#pragma once

// The two kinds of row this console shows, and how they are drawn.
//
// This is the whole of what a widget is in PRISM: a value type with its own `Widget<T>`
// specialization. No base class, no virtual function, no widget to construct and keep -- the
// virtual list holds `ConsoleLine`s and paints them, and `Widget<ConsoleLine>::record` is what
// "paint" means. Swap the type in the list and the row looks different; nothing else changes.

#include <prism/prism.hpp>

#include "runner_model.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace console_rows {

using namespace prism;

inline const core::Color kBackground = core::Color::rgba(16, 18, 22);
inline const core::Color kOk = core::Color::rgba(120, 205, 140);
inline const core::Color kWarn = core::Color::rgba(235, 180, 80);

inline constexpr float kConsoleRowHeight = 19.f;
inline constexpr float kTaskRowHeight = 26.f;
inline constexpr float kConsoleGlyph = 12.5f;
inline constexpr float kTaskGlyph = 13.5f;
inline constexpr float kPad = 8.f;
inline constexpr float kStripe = 2.f;
inline constexpr float kTaskStripe = 3.f;

// A console line's colour: the theme for what it is not worth a colour of its own (a terminal is
// dark whatever the window theme is), two local constants for "careful" and "it worked".
inline core::Color line_colour(const ui::Theme& theme, runner::LineKind kind) {
    switch (kind) {
    case runner::LineKind::Error: return theme.error;
    case runner::LineKind::Warning: return kWarn;
    case runner::LineKind::Success: return kOk;
    case runner::LineKind::Command: return theme.accent_hover;
    case runner::LineKind::Info: break;
    }
    return theme.text;
}

// A task's state, and the colour and word the task list shows for it.
enum class TaskState : std::uint8_t { Idle, Running, Passed, Failed, Stopped };

inline core::Color state_colour(const ui::Theme& theme, TaskState state) {
    switch (state) {
    case TaskState::Running: return theme.accent_hover;
    case TaskState::Passed: return kOk;
    case TaskState::Failed: return theme.error;
    case TaskState::Stopped: return kWarn;
    case TaskState::Idle: break;
    }
    return theme.text_muted;
}

inline std::string_view state_label(TaskState state) {
    switch (state) {
    case TaskState::Running: return "running";
    case TaskState::Passed: return "ok";
    case TaskState::Failed: return "failed";
    case TaskState::Stopped: return "stopped";
    case TaskState::Idle: break;
    }
    return "not run";
}

// One row of the task list. The state is in the value, not looked up while painting, so a row
// changes only when `List::set` says it did -- and `==` is all that takes.
struct TaskRow {
    std::string text{};
    TaskState state = TaskState::Idle;
    bool operator==(const TaskRow&) const = default;
};

} // namespace console_rows

namespace prism::ui {

// A console line: the terminal background, a stripe in the line's own colour, the text in the
// mono font. Rows are measured from the rectangle this draws, which is why it has a fixed height.
template <>
struct Widget<runner::ConsoleLine> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height widget_h{console_rows::kConsoleRowHeight};

    static void record(DrawList& dl, const Field<runner::ConsoleLine>& field, WidgetNode& node) {
        const auto& line = field.get();
        const auto& theme = node_theme(node);
        const auto colour = console_rows::line_colour(theme, line.kind);

        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, delegate_detail::widget_w(node), widget_h),
                       console_rows::kBackground);
        if (line.kind != runner::LineKind::Info)
            dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, Width{console_rows::kStripe}, widget_h), colour);
        dl.text(line.text,
                delegate_detail::make_point(X{console_rows::kPad},
                                            Y{(widget_h.raw() - console_rows::kConsoleGlyph) * 0.5f}),
                console_rows::kConsoleGlyph, colour, 0.f, TextAnchor::TopLeft, FontRole::Mono);
    }

    static void handle_input(Field<runner::ConsoleLine>&, const InputEvent&, WidgetNode&) {}
};

// A task: its name and state in the row's colour, with a stripe in the same colour.
template <>
struct Widget<console_rows::TaskRow> {
    static constexpr FocusPolicy focus_policy = FocusPolicy::none;
    static constexpr Height widget_h{console_rows::kTaskRowHeight};

    static void record(DrawList& dl, const Field<console_rows::TaskRow>& field, WidgetNode& node) {
        const auto& row = field.get();
        const auto colour = console_rows::state_colour(node_theme(node), row.state);
        // Filling the whole row is what makes the whole row clickable: a leaf's hit rect is the
        // bounds of what it draws, not its layout box.
        delegate_detail::fill_widget_bg(dl, node, widget_h);
        dl.filled_rect(delegate_detail::make_rect(X{0}, Y{0}, Width{console_rows::kTaskStripe}, widget_h), colour);
        dl.text(row.text,
                delegate_detail::make_point(X{console_rows::kPad},
                                            Y{(widget_h.raw() - console_rows::kTaskGlyph) * 0.5f}),
                console_rows::kTaskGlyph, colour);
    }

    static void handle_input(Field<console_rows::TaskRow>&, const InputEvent&, WidgetNode&) {}
};

} // namespace prism::ui
