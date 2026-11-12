#pragma once

// The system's folder picker with SDL_ShowOpenFolderDialog's contract: `callback` gets {folder, nullptr} after a
// choice, {nullptr} after a cancel, nullptr (and SDL_GetError) on an error, possibly on another thread.
// On Windows it is the shell's file dialog picking folders (IFileOpenDialog with FOS_PICKFOLDERS: the Explorer-style
// window with the address bar, Quick access and search), not SDL 3.2's older "Browse For Folder" tree
// (SHBrowseForFolder). Where that dialog cannot be made, and on other systems, SDL's own dialog is used.

#include <SDL3/SDL_dialog.h>

namespace hailstone::platform {

void show_open_folder_dialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const char* start);

} // namespace hailstone::platform
