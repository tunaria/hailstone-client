#pragma once

// The system's folder picker (show_open_folder_dialog: the portal or GTK on Linux, the shell's file dialog on Windows).
// It answers later, possibly on another thread: poll() gives the folder once, after the player chose one.

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace hailstone::platform {

class Window;

class FolderDialog {
public:
    FolderDialog();
    ~FolderDialog();
    FolderDialog(const FolderDialog&) = delete;
    FolderDialog& operator=(const FolderDialog&) = delete;

    // Opens the picker over `window` at `start` (empty: the system's choice); nothing when one is open already.
    void open(Window& window, const std::filesystem::path& start);
    bool is_open() const;
    // The folder chosen since the last call, once; nullopt while open, after a cancel, or when none was opened.
    std::optional<std::filesystem::path> poll();

    struct Shared; // held by the dialog's callback too, so a late answer finds it

private:
    std::shared_ptr<Shared> shared_;
};

} // namespace hailstone::platform
