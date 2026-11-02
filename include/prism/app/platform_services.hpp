#pragma once

#include <prism/app/window.hpp>

#include <functional>
#include <string>
#include <vector>

// What an app asks of the platform beyond drawing and input: file dialogs and message boxes.
// One interface, one implementation per platform layer (the SDL backend's), chosen at build
// time -- the shape of Qt's platform plugins, see doc/review-2026-11-02.md §4. A request may
// come from any thread and never blocks; an answer may come back on any thread. Apps use
// AppContext::choose_files / show_message, which bring the answer to the logic thread.
namespace prism::app {

struct FileFilter {
    std::string name;       // "Text files"
    std::string patterns;   // extensions separated by ';' ("txt;md"), or "*" for any file
};

enum class FileDialogKind { Open, OpenMany, Save, Folder };

struct FileDialog {
    FileDialogKind kind = FileDialogKind::Open;
    std::string title;
    std::vector<FileFilter> filters;
    std::string start;   // folder (or file, for Save) to start from; empty: the platform's choice
};

struct FileDialogResult {
    std::vector<std::string> paths;   // empty: cancelled, or failed (then `error` says why)
    std::string error;
};

enum class MessageKind { Info, Warning, Error };

struct PlatformServices {
    virtual ~PlatformServices() = default;
    // Calls `done` exactly once, on any thread, when the user closes the dialog.
    virtual void show_file_dialog(WindowId parent, FileDialog dialog,
                                  std::function<void(FileDialogResult)> done) = 0;
    // For what the app cannot show in its own window (it may block the platform's UI thread
    // while it is open, as native message boxes do).
    virtual void show_message(WindowId parent, MessageKind kind, std::string title, std::string text) = 0;
};

} // namespace prism::app
