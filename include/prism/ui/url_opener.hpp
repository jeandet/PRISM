#pragma once

#include <string>

namespace prism::ui {

// Opens a URL in the user's browser (or the platform's handler for it). The backend owns one
// and hands it to the widget tree through Theme::url_opener, as it does the clipboard. open()
// is called on the logic thread and must not block.
struct UrlOpener {
    virtual ~UrlOpener() = default;
    virtual void open(std::string url) const = 0;
};

} // namespace prism::ui
