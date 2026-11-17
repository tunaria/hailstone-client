#pragma once

// Which asset folder the client starts with, and the folders the asset-folder picker remembers. hailstone.ini keys:
//   assets.path    the folder last chosen in the picker
//   assets.recent  the folders chosen, most recent first, '|'-separated (at most kMaxRecentAssetFolders)
//   assets.pick    1: show the picker on the idle login screen, or at the next start, even when assets.path is
//                  fine (the Options window's "Change asset folder")
// --game-dir and EQ_GAME_DIR come first: a valid one is saved as assets.path (the caller saves it), so the picker can
// change it later like any saved folder.

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/asset_folder.h"

namespace hailstone {

class Config;

inline constexpr std::size_t kMaxRecentAssetFolders = 8;

enum class AssetFolderSource {
    Override, // --game-dir or EQ_GAME_DIR, valid; the caller saves it as assets.path
    Config,   // hailstone.ini assets.path
    Found,    // the game folder Hailstone's own folder sits in (the old <game>/hailstone layout); not saved
    Sibling,  // the one game folder beside Hailstone's own folder (<parent>/hailstone next to <parent>/<game>); saved
    Picker,   // nothing usable: the picker asks
};

struct AssetFolderResolution {
    AssetFolderSource source = AssetFolderSource::Picker;
    // The folder to use; for the picker the one it starts on (the saved assets.path, which may be wrong), or empty.
    std::filesystem::path path;
    // Why the picker shows, for its status line ("" on a first start, or when a folder was found).
    std::string reason;
    // The check of a saved assets.path that failed it (what the log names), else empty.
    AssetFolderCheck check;
    // An override that was given but not used, and why (for the log), else empty.
    std::string override_ignored;
    // With the picker: the game folders found beside Hailstone's own folder (more than one), to list first.
    std::vector<std::filesystem::path> candidates;
};

// Checks a folder; validate_asset_folder() unless a test passes its own.
using AssetFolderValidator = std::function<AssetFolderCheck(const std::filesystem::path&)>;

// The order: the override (`override_dir`, from --game-dir or EQ_GAME_DIR) when it passes the check and does not
// hold `data_folder`; one that does not is ignored (override_ignored says why). Then assets.path when it passes the
// check, no assets.pick is set and it does not hold `data_folder` (data_folder_conflict; an empty one is not checked).
// Then, when `own_folder` (Hailstone's own folder; passed only in a HAILSTONE_ALLOW_IN_EQ_FOLDER build) is given and
// no assets.pick is set: find_sibling_asset_folders() of it. Exactly one: Sibling. Several: the picker with them as
// candidates. None: the parent of `own_folder` (the old <game>/hailstone layout) as Found when it passes the check and
// does not hold `data_folder` by data_folder_conflict's subfolder rule. Else the picker.
AssetFolderResolution resolve_asset_folder(const std::filesystem::path& override_dir, const Config& config,
                                           const AssetFolderValidator& validate = {},
                                           const std::filesystem::path& data_folder = {},
                                           const std::filesystem::path& own_folder = {});

// The folder that holds `own_folder` (a trailing separator ignored), or empty.
std::filesystem::path parent_of_own_folder(const std::filesystem::path& own_folder);

// The direct subfolders of the parent of `own_folder` (Hailstone's own folder, a trailing separator allowed), other
// than `own_folder` itself, that pass `validate` (validate_asset_folder() when empty) and do not hold `data_folder`
// (an empty one is not checked). Sorted by name.
std::vector<std::filesystem::path> find_sibling_asset_folders(const std::filesystem::path& own_folder,
                                                              const AssetFolderValidator& validate = {},
                                                              const std::filesystem::path& data_folder = {});

// True in a build made with the CMake option HAILSTONE_ALLOW_IN_EQ_FOLDER (off by default): Hailstone's folder may
// then be a subfolder of the EverQuest folder (data_folder_conflict).
bool data_folder_subfolder_allowed();

// Why `asset_folder` cannot be the asset folder when Hailstone writes into `data_folder` (platform::data_dir()):
// the data folder is that folder or inside it, so Hailstone would write into the installation it must only read.
// "" when they are apart. With `allow_subfolder` a data folder inside it (<EverQuest>/hailstone/, as a server's
// patcher may install it) is "" too; only the same folder is refused. Both are made absolute and normal first (the
// disk is asked for links where it can be).
std::string data_folder_conflict(const std::filesystem::path& asset_folder, const std::filesystem::path& data_folder,
                                 bool allow_subfolder = data_folder_subfolder_allowed());

// True when two folder names are the same folder as written: compared after lexically_normal() and without a
// trailing separator, case-insensitively on Windows. The disk is not asked.
bool same_folder(const std::filesystem::path& a, const std::filesystem::path& b);

// assets.recent split into its folders, empty ones and repeats left out.
std::vector<std::string> recent_asset_folders(const Config& config);
// A stored list (as join_recent_folders writes it) split into its folders, empty ones and repeats left out.
std::vector<std::string> split_recent_folders(std::string_view all);
// `folder` put first in `recent`, an earlier entry for the same folder removed, cut to `max` entries.
std::vector<std::string> push_recent_folder(std::vector<std::string> recent, std::string_view folder,
                                            std::size_t max = kMaxRecentAssetFolders);
// `folder` taken out of `recent` (the picker's "Remove" on a folder that is gone).
std::vector<std::string> remove_recent_folder(std::vector<std::string> recent, std::string_view folder);
// The list as assets.recent stores it.
std::string join_recent_folders(const std::vector<std::string>& recent);

// The picker's choice: assets.path = `folder` (made absolute against the current directory), it first in
// assets.recent, and assets.pick cleared. The caller saves the config.
void remember_asset_folder(Config& config, const std::filesystem::path& folder);
// Sets (or clears) assets.pick. The caller saves the config.
void request_asset_folder_pick(Config& config, bool pick);
bool asset_folder_pick_requested(const Config& config);

} // namespace hailstone
