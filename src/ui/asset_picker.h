#pragma once

// The asset-folder picker: the screen shown at start-up, before the login, when no usable RoF2 asset folder is
// known (core/asset_folder_choice.h). The player chooses a folder with the system's folder dialog, from the
// folders chosen before, or by typing it; Continue is offered once the folder passes validate_asset_folder().

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/asset_folder.h"

struct SDL_Window;
namespace hailstone::platform { class Window; }
namespace hailstone::render { class VkContext; }

namespace hailstone::ui {

class ImGuiLayer;

// The system's folder dialog (platform::show_open_folder_dialog). It answers asynchronously, possibly on another
// thread; take() hands the answer to the UI thread.
class FolderDialog {
public:
    FolderDialog();
    // Opens the dialog over `window`, starting in `start` (may be empty). Does nothing while one is open.
    void open(SDL_Window* window, const std::string& start);
    bool is_open() const;
    struct Result {
        std::optional<std::string> folder; // nullopt: cancelled, or failed (error says why)
        std::string error;
    };
    // The dialog's answer, once, after it closed.
    std::optional<Result> take();

private:
    struct State;
    std::shared_ptr<State> state_; // shared with the callback, which may come after this is gone
};

struct AssetPickerView {
    std::string folder;              // the folder looked at (typed, picked or from the recent list)
    std::optional<AssetFolderCheck> check; // its check; nullopt until there is a folder
    std::vector<std::string> recent; // the folders chosen before, most recent first
    std::string reason;              // why the picker shows (resolve_asset_folder)
    std::string error;               // the folder dialog's failure, if any
    std::filesystem::path data_folder; // Hailstone's data folder (platform::data_dir()), which may not be inside
    std::string conflict;            // why the folder cannot be used although it passed the check (data_folder_conflict)
    bool cancel = false;             // Cancel in place of Quit: the game goes on with its folder (the login screen's)
    bool window_closed = false;      // run_asset_picker ended because the window was closed
};

enum class AssetPickerAction { None, Browse, Continue, Quit };

// Looks at `folder`: sets view.folder and checks it.
void set_picker_folder(AssetPickerView& view, std::string folder);

// Draws the picker; call between ImGuiLayer::new_frame() and render(). Choosing a recent folder or typing one
// (Enter) checks it at once. Continue is only returned for a folder that passed the check (warnings allowed).
// `dialog_open` disables Browse while the folder dialog is up.
AssetPickerAction asset_picker_screen(AssetPickerView& view, bool dialog_open);

// The picker's own frame loop, with the client's window and Vulkan context and no game: until the player
// continues with a usable folder (returned) or quits, cancels or closes the window (nullopt; view.window_closed
// tells the last apart). `max_frames` > 0 gives up after that many frames,
// saving the last one as PNG to `screenshot` when given (test runs).
std::optional<std::filesystem::path> run_asset_picker(platform::Window& window, render::VkContext& vk,
                                                      ImGuiLayer& imgui, AssetPickerView& view, int max_frames = 0,
                                                      const std::string& screenshot = {});

} // namespace hailstone::ui
