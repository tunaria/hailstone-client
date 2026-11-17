#include "core/asset_folder_choice.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "core/config.h"

namespace hailstone {

namespace {
constexpr char kRecentSeparator = '|'; // not allowed in a Windows path, and rare elsewhere

std::string folder_key(const std::filesystem::path& p) {
    // As UTF-8: generic_string() throws on Windows for a name the ANSI code page cannot show.
    const std::u8string u = p.lexically_normal().generic_u8string();
    std::string s(u.begin(), u.end());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#ifdef _WIN32
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}
} // namespace

// assets.path alone (resolve_asset_folder without the override and `own_folder`).
static AssetFolderResolution resolve_saved_asset_folder(const Config& config, const AssetFolderValidator& check,
                                                        const std::filesystem::path& data_folder);

std::filesystem::path parent_of_own_folder(const std::filesystem::path& own_folder) {
    std::filesystem::path own = own_folder;
    if (!own.has_filename() && own.has_relative_path()) own = own.parent_path(); // "/games/hailstone/"
    return own.parent_path();
}

std::vector<std::filesystem::path> find_sibling_asset_folders(const std::filesystem::path& own_folder,
                                                              const AssetFolderValidator& validate,
                                                              const std::filesystem::path& data_folder) {
    namespace fs = std::filesystem;
    std::vector<fs::path> out;
    const fs::path parent = parent_of_own_folder(own_folder);
    if (parent.empty()) return out;
    std::error_code ec;
    fs::directory_iterator it(parent, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code dir_ec;
        if (!it->is_directory(dir_ec)) continue;
        const fs::path p = it->path();
        if (same_folder(p, own_folder)) continue;
        if (!(validate ? validate(p) : validate_asset_folder(p)).ok) continue;
        if (!data_folder.empty() && !data_folder_conflict(p, data_folder, true).empty()) continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

AssetFolderResolution resolve_asset_folder(const std::filesystem::path& override_dir, const Config& config,
                                           const AssetFolderValidator& validate,
                                           const std::filesystem::path& data_folder,
                                           const std::filesystem::path& own_folder) {
    auto check = [&](const std::filesystem::path& p) { return validate ? validate(p) : validate_asset_folder(p); };
    // One setting, assets.path: a saved folder that works (or a requested pick) comes first, and the override is
    // only the default that fills an empty or broken one.
    AssetFolderResolution out = resolve_saved_asset_folder(config, check, data_folder);
    if (!override_dir.empty()) {
        std::error_code ec;
        if (out.source == AssetFolderSource::Config) {
            out.override_ignored = "assets.path " + out.path.string() + " is saved";
        } else if (asset_folder_pick_requested(config)) {
            out.override_ignored = "the asset folder picker was asked for";
        } else if (!std::filesystem::is_directory(override_dir, ec)) {
            out.override_ignored = "not a folder";
        } else if (!check(override_dir).ok) {
            out.override_ignored = "not a complete EverQuest installation (RoF2 or live)";
        } else if (std::string why = data_folder.empty() ? std::string() : data_folder_conflict(override_dir, data_folder);
                   !why.empty()) {
            out.override_ignored = why;
        } else {
            AssetFolderResolution used;
            used.source = AssetFolderSource::Override;
            used.path = override_dir;
            return used;
        }
    }
    if (out.source != AssetFolderSource::Picker || own_folder.empty() || asset_folder_pick_requested(config)) return out;
    // Nothing saved works (a first start, or a moved folder): the game folder beside Hailstone's own folder.
    std::vector<std::filesystem::path> siblings = find_sibling_asset_folders(own_folder, check, data_folder);
    if (siblings.size() == 1) {
        out.source = AssetFolderSource::Sibling;
        out.path = siblings.front();
        out.reason.clear();
        out.check = {};
        return out;
    }
    if (siblings.size() > 1) {
        out.candidates = std::move(siblings);
        if (out.reason.empty()) out.reason = "More than one game folder is next to Hailstone's folder: choose one.";
        return out;
    }
    // None: the old layout, Hailstone inside the game folder.
    const std::filesystem::path parent = parent_of_own_folder(own_folder);
    std::error_code ec;
    if (!parent.empty() && std::filesystem::is_directory(parent, ec) && check(parent).ok &&
        (data_folder.empty() || data_folder_conflict(parent, data_folder, true).empty())) {
        out.source = AssetFolderSource::Found;
        out.path = parent;
        out.reason.clear();
        out.check = {};
    }
    return out;
}

static AssetFolderResolution resolve_saved_asset_folder(const Config& config, const AssetFolderValidator& check,
                                                        const std::filesystem::path& data_folder) {
    AssetFolderResolution out;
    std::filesystem::path saved = config.get_string("assets.path", "");
    out.path = saved;
    if (saved.empty()) return out;
    if (asset_folder_pick_requested(config)) {
        out.reason = "Choose the asset folder to play with.";
        return out;
    }
    if (!data_folder.empty())
        if (std::string why = data_folder_conflict(saved, data_folder); !why.empty()) {
            out.reason = why;
            return out;
        }
    AssetFolderCheck c = check(saved);
    if (c.ok) {
        out.source = AssetFolderSource::Config;
        return out;
    }
    out.reason = c.is_directory ? "The saved asset folder cannot be used: it is not a complete EverQuest installation (RoF2 or live)."
                                : "The saved asset folder is not there any more.";
    out.check = std::move(c);
    return out;
}

bool data_folder_subfolder_allowed() {
#if defined(HAILSTONE_ALLOW_IN_EQ_FOLDER) && HAILSTONE_ALLOW_IN_EQ_FOLDER
    return true;
#else
    return false;
#endif
}

std::string data_folder_conflict(const std::filesystem::path& asset_folder, const std::filesystem::path& data_folder,
                                 bool allow_subfolder) {
    namespace fs = std::filesystem;
    if (asset_folder.empty() || data_folder.empty()) return {};
    auto normal = [](const fs::path& p) {
        std::error_code ec;
        fs::path out = fs::weakly_canonical(p, ec);
        if (ec) out = fs::absolute(p, ec).lexically_normal();
        return fs::path(folder_key(out));
    };
    fs::path rel = normal(data_folder).lexically_relative(normal(asset_folder));
    if (rel.empty() || *rel.begin() == "..") return {};
    if (rel == ".")
        return "Hailstone keeps its own files in " + data_folder.string() +
               ", this same folder, and would write into the installation, which it only reads. Move Hailstone into "
               "a folder of its own (or set HAILSTONE_DATA_DIR) and start it again.";
    // A folder of Hailstone's own inside the installation (<EverQuest>/hailstone/, where a server's patcher may install
    // it): writing there changes none of the installation's files, and game files are read only from the
    // installation's top folder and its known subfolders. Allowed only in builds made with HAILSTONE_ALLOW_IN_EQ_FOLDER.
    if (allow_subfolder) return {};
    return "Hailstone keeps its own files in " + data_folder.string() +
           ", inside this folder, and would write into the installation, which it only reads. Move Hailstone out of "
           "the EverQuest folder (or set HAILSTONE_DATA_DIR) and start it again.";
}

bool same_folder(const std::filesystem::path& a, const std::filesystem::path& b) {
    return folder_key(a) == folder_key(b);
}

std::vector<std::string> recent_asset_folders(const Config& config) {
    return split_recent_folders(config.get_string("assets.recent", ""));
}

std::vector<std::string> split_recent_folders(std::string_view all) {
    std::vector<std::string> out;
    for (std::size_t start = 0; start <= all.size();) {
        std::size_t end = std::min(all.find(kRecentSeparator, start), all.size());
        std::string one(all.substr(start, end - start));
        start = end + 1;
        if (one.empty()) continue;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& r) { return same_folder(r, one); }))
            out.push_back(std::move(one));
    }
    return out;
}

std::vector<std::string> push_recent_folder(std::vector<std::string> recent, std::string_view folder, std::size_t max) {
    recent = remove_recent_folder(std::move(recent), folder);
    if (!folder.empty()) recent.insert(recent.begin(), std::string(folder));
    if (recent.size() > max) recent.resize(max);
    return recent;
}

std::vector<std::string> remove_recent_folder(std::vector<std::string> recent, std::string_view folder) {
    std::filesystem::path f(folder);
    std::erase_if(recent, [&](const std::string& r) { return same_folder(r, f); });
    return recent;
}

std::string join_recent_folders(const std::vector<std::string>& recent) {
    std::string out;
    for (const auto& r : recent) {
        if (r.empty() || r.find(kRecentSeparator) != std::string::npos) continue; // would not read back as one
        if (!out.empty()) out += kRecentSeparator;
        out += r;
    }
    return out;
}

void remember_asset_folder(Config& config, const std::filesystem::path& folder) {
    // Made absolute: a typed "../RoF2" would mean another folder when the client starts from another directory.
    std::error_code ec;
    std::filesystem::path abs = std::filesystem::absolute(folder, ec);
    std::filesystem::path p = ec ? folder : abs.lexically_normal();
    if (!p.has_filename() && p.has_relative_path()) p = p.parent_path(); // "/games/RoF2/" -> "/games/RoF2"
    std::string text = p.string();
    config.set("assets.path", text);
    config.set("assets.recent", join_recent_folders(push_recent_folder(recent_asset_folders(config), text)));
    request_asset_folder_pick(config, false);
}

void request_asset_folder_pick(Config& config, bool pick) {
    if (pick) config.set_bool("assets.pick", true);
    else config.erase("assets.pick");
}

bool asset_folder_pick_requested(const Config& config) { return config.get_bool("assets.pick", false); }

} // namespace hailstone
