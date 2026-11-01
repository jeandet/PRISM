#pragma once

#include <string>

namespace prism::ui {

// Where copied text goes. Backends implement it (the system clipboard usually belongs to one
// thread) and hand it to the widget tree through Theme::clipboard. set_text is called on the
// logic thread and must not block. Pasting comes the other way, as an input::Paste event the
// backend produces, so no widget ever waits for the clipboard.
struct Clipboard {
    virtual ~Clipboard() = default;
    virtual void set_text(std::string utf8) const = 0;
};

} // namespace prism::ui
