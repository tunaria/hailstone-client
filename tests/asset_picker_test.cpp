// The asset-folder choice at start-up and the picker's recent folders (core/asset_folder_choice.h).
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/asset_folder.h"
#include "core/asset_folder_choice.h"
#include "core/config.h"
#include "core/game_dir.h"

namespace fs = std::filesystem;
using namespace hailstone;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / (std::string("hailstone_picker_") + tag + "_" + std::to_string(stamp));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// A validator that passes only `good`, so the tests need no RoF2 files.
AssetFolderValidator only(const fs::path& good) {
    return [good](const fs::path& p) {
        AssetFolderCheck c;
        c.is_directory = true;
        c.ok = same_folder(p, good);
        if (!c.ok) c.missing.push_back("spells_us.txt");
        return c;
    };
}
} // namespace

TEST_CASE("Asset folder: a valid --game-dir / EQ_GAME_DIR first, then assets.path, then the picker") {
    TempDir a("a"), b("b");
    Config config;

    SUBCASE("nothing known: the picker, with no complaint") {
        auto r = resolve_asset_folder({}, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.path.empty());
        CHECK(r.reason.empty());
    }
    SUBCASE("a usable assets.path") {
        config.set("assets.path", a.path.string());
        auto r = resolve_asset_folder({}, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Config);
        CHECK(r.path == a.path);
    }
    SUBCASE("a valid override wins over assets.path") {
        config.set("assets.path", b.path.string());
        auto r = resolve_asset_folder(a.path, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Override);
        CHECK(r.path == a.path);
        CHECK(r.override_ignored.empty());
    }
    SUBCASE("an override that fails the check is ignored: assets.path is used") {
        config.set("assets.path", a.path.string());
        auto r = resolve_asset_folder(b.path, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Config);
        CHECK(r.path == a.path);
        CHECK_FALSE(r.override_ignored.empty());
    }
    SUBCASE("an override that is no folder is ignored: assets.path is used") {
        config.set("assets.path", a.path.string());
        auto r = resolve_asset_folder(a.path / "nope", config, only(a.path));
        CHECK(r.source == AssetFolderSource::Config);
        CHECK(r.path == a.path);
        CHECK(r.override_ignored == "not a folder");
    }
    SUBCASE("an ignored override with nothing saved: the picker") {
        auto r = resolve_asset_folder(b.path, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK_FALSE(r.override_ignored.empty());
    }
    SUBCASE("an override that holds the data folder is ignored") {
        auto r = resolve_asset_folder(a.path, config, only(a.path), a.path);
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.override_ignored.find("this same folder") != std::string::npos);
    }
    SUBCASE("an assets.path that fails the check: the picker, saying what is missing") {
        config.set("assets.path", b.path.string());
        auto r = resolve_asset_folder({}, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.path == b.path);
        CHECK_FALSE(r.reason.empty());
        CHECK(r.check.missing == std::vector<std::string>{"spells_us.txt"});
    }
    SUBCASE("assets.pick shows the picker over a usable folder, until a folder is remembered") {
        config.set("assets.path", a.path.string());
        request_asset_folder_pick(config, true);
        CHECK(asset_folder_pick_requested(config));
        auto r = resolve_asset_folder({}, config, only(a.path));
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.path == a.path);
        CHECK_FALSE(r.reason.empty());
        // A valid override still comes first.
        CHECK(resolve_asset_folder(a.path, config, only(a.path)).source == AssetFolderSource::Override);
        remember_asset_folder(config, a.path);
        CHECK_FALSE(asset_folder_pick_requested(config));
        CHECK_FALSE(config.get("assets.pick"));
        CHECK(resolve_asset_folder({}, config, only(a.path)).source == AssetFolderSource::Config);
    }
}

TEST_CASE("Asset folder: the recent folders") {
    using V = std::vector<std::string>;
    CHECK(push_recent_folder({}, "/eq") == V{"/eq"});
    CHECK(push_recent_folder({"/a", "/b"}, "/c") == V{"/c", "/a", "/b"});
    // The same folder moves to the front, also when written with a trailing separator or a dot.
    CHECK(push_recent_folder({"/a", "/b", "/c"}, "/b") == V{"/b", "/a", "/c"});
    CHECK(push_recent_folder({"/a", "/b/", "/c"}, "/./b") == V{"/./b", "/a", "/c"});
    CHECK(push_recent_folder({"/a"}, "") == V{"/a"});
    // At most `max`, the oldest dropped.
    V many;
    for (int i = 0; i < 10; ++i) many = push_recent_folder(many, "/f" + std::to_string(i));
    REQUIRE(many.size() == kMaxRecentAssetFolders);
    CHECK(many.front() == "/f9");
    CHECK(many.back() == "/f2");
    CHECK(push_recent_folder({"/a", "/b", "/c"}, "/d", 2) == V{"/d", "/a"});

    CHECK(remove_recent_folder({"/a", "/b", "/c"}, "/b/") == V{"/a", "/c"});
    CHECK(remove_recent_folder({"/a"}, "/x") == V{"/a"});

    CHECK(same_folder("/eq/rof2", "/eq/rof2/"));
    CHECK(same_folder("/eq/x/../rof2", "/eq/rof2"));
    CHECK_FALSE(same_folder("/eq/rof2", "/eq/rof"));
#ifndef _WIN32
    CHECK_FALSE(same_folder("/eq/RoF2", "/eq/rof2")); // case matters off Windows
#endif

    // assets.recent: '|'-separated, empty entries and repeats skipped on reading.
    Config config;
    config.set("assets.recent", "/a||/b|/a/|/c");
    CHECK(recent_asset_folders(config) == V{"/a", "/b", "/c"});
    CHECK(join_recent_folders({"/a", "", "/b|c", "/d"}) == "/a|/d");

    // remember_asset_folder: assets.path, the folder first in assets.recent, both absolute.
    const std::string b = fs::absolute("/b").string(), fresh = fs::absolute("/new").string();
    config.set("assets.recent", "/a|" + b + "|/c");
    remember_asset_folder(config, "/b/");
    CHECK(config.get_string("assets.path") == b);
    CHECK(config.get_string("assets.recent") == b + "|/a|/c");
    remember_asset_folder(config, "/new");
    CHECK(recent_asset_folders(config) == V{fresh, b, "/a", "/c"});
    // A typed relative folder is kept as the absolute one it meant when it was chosen.
    remember_asset_folder(config, fs::path("games") / ".." / "RoF2");
    CHECK(config.get_string("assets.path") == (fs::current_path() / "RoF2").string());
}

TEST_CASE("Asset folder: --game-dir is saved like a picker choice and can be changed") {
    TempDir a("gd_a"), b("gd_b");
    auto both = [&](const fs::path& p) {
        AssetFolderCheck c;
        c.is_directory = true;
        c.ok = same_folder(p, a.path) || same_folder(p, b.path);
        return c;
    };
    Config config;
    // Started with --game-dir a: used, and the caller (choose_asset_folder) saves it as the picker would.
    auto r = resolve_asset_folder(a.path, config, both);
    REQUIRE(r.source == AssetFolderSource::Override);
    remember_asset_folder(config, r.path);
    CHECK(same_folder(config.get_string("assets.path"), fs::absolute(a.path)));
    REQUIRE_FALSE(recent_asset_folders(config).empty());
    CHECK(same_folder(recent_asset_folders(config).front(), fs::absolute(a.path)));
    // The next start without it: the saved folder.
    CHECK(resolve_asset_folder({}, config, both).source == AssetFolderSource::Config);
    // "Change Asset Folder" is allowed: the picker shows, and its choice replaces the saved one.
    request_asset_folder_pick(config, true);
    CHECK(resolve_asset_folder({}, config, both).source == AssetFolderSource::Picker);
    remember_asset_folder(config, b.path);
    auto after = resolve_asset_folder({}, config, both);
    CHECK(after.source == AssetFolderSource::Config);
    CHECK(same_folder(after.path, fs::absolute(b.path)));
    CHECK(recent_asset_folders(config).size() == 2);
    // An invalid --game-dir later changes nothing saved.
    auto bad = resolve_asset_folder(a.path / "nope", config, both);
    CHECK(bad.source == AssetFolderSource::Config);
    CHECK(same_folder(bad.path, fs::absolute(b.path)));
}

TEST_CASE("Asset folder: the game folders beside Hailstone's own folder") {
    TempDir tmp("siblings");
    const fs::path hs = tmp.path / "hailstone";
    const fs::path client = tmp.path / "NMS Client";
    const fs::path junk = tmp.path / "Downloads";
    fs::create_directories(hs);
    fs::create_directories(client);
    fs::create_directories(junk);
    std::ofstream(tmp.path / "readme.txt") << "x"; // a file, not a folder
    Config config;
    // A validator that also passes Hailstone's own folder: it must still never be a candidate.
    auto pass = [&](std::vector<fs::path> good) {
        return [good](const fs::path& p) {
            AssetFolderCheck c;
            c.is_directory = true;
            for (const auto& g : good) c.ok = c.ok || same_folder(p, g);
            return c;
        };
    };

    SUBCASE("one: used and marked to be saved") {
        auto v = pass({client, hs});
        CHECK(find_sibling_asset_folders(hs, v) == std::vector<fs::path>{client});
        auto r = resolve_asset_folder({}, config, v, hs / "data", hs);
        CHECK(r.source == AssetFolderSource::Sibling);
        CHECK(same_folder(r.path, client));
        CHECK(r.reason.empty());
    }
    SUBCASE("a trailing separator on Hailstone's folder") {
        auto v = pass({client, hs});
        CHECK(same_folder(parent_of_own_folder(hs / ""), tmp.path));
        CHECK(find_sibling_asset_folders(hs / "", v) == std::vector<fs::path>{client});
        auto r = resolve_asset_folder({}, config, v, {}, hs / "");
        CHECK(r.source == AssetFolderSource::Sibling);
        CHECK(same_folder(r.path, client));
    }
    SUBCASE("none: the folder Hailstone sits in (the old layout), not saved") {
        auto v = pass({tmp.path, hs});
        CHECK(find_sibling_asset_folders(hs, v).empty());
        auto r = resolve_asset_folder({}, config, v, hs / "data", hs);
        CHECK(r.source == AssetFolderSource::Found);
        CHECK(same_folder(r.path, tmp.path));
    }
    SUBCASE("none, and the parent is no game folder either: the picker") {
        auto r = resolve_asset_folder({}, config, pass({hs}), {}, hs);
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.candidates.empty());
    }
    SUBCASE("several: the picker, with them as candidates") {
        auto v = pass({client, junk, hs});
        auto r = resolve_asset_folder({}, config, v, {}, hs);
        CHECK(r.source == AssetFolderSource::Picker);
        CHECK(r.candidates == std::vector<fs::path>{junk, client}); // sorted by name
        CHECK_FALSE(r.reason.empty());
    }
    SUBCASE("a saved folder that works wins; assets.pick shows the picker") {
        auto v = pass({client, junk});
        config.set("assets.path", junk.string());
        CHECK(resolve_asset_folder({}, config, v, {}, hs).source == AssetFolderSource::Config);
        config.set("assets.path", (tmp.path / "moved").string());
        CHECK(resolve_asset_folder({}, config, pass({client}), {}, hs).source == AssetFolderSource::Sibling);
        request_asset_folder_pick(config, true);
        CHECK(resolve_asset_folder({}, config, pass({client}), {}, hs).source == AssetFolderSource::Picker);
    }
    SUBCASE("no own folder (a build without HAILSTONE_ALLOW_IN_EQ_FOLDER): the picker") {
        CHECK(resolve_asset_folder({}, config, pass({client}), {}).source == AssetFolderSource::Picker);
    }
    SUBCASE("a valid --game-dir comes before the siblings") {
        auto r = resolve_asset_folder(junk, config, pass({client, junk}), {}, hs);
        CHECK(r.source == AssetFolderSource::Override);
        CHECK(r.path == junk);
    }
}

TEST_CASE("Asset folder: the game folder Hailstone sits in (old layout) when nothing saved works") {
    TempDir tmp("beside");
    const fs::path eq = tmp.path / "EverQuest";
    const fs::path hs = eq / "hailstone";
    fs::create_directories(hs);
    Config config;
    // A first start: found, and not saved.
    auto r = resolve_asset_folder({}, config, only(eq), hs, hs);
    CHECK(r.source == AssetFolderSource::Found);
    CHECK(same_folder(r.path, eq));
    CHECK(r.reason.empty());
    CHECK(config.get_string("assets.path", "").empty());
    // Not an installation: the picker, as before.
    CHECK(resolve_asset_folder({}, config, only(tmp.path / "other"), hs, hs).source == AssetFolderSource::Picker);
    // Hailstone's data folder is the game folder itself: refused even as a default.
    CHECK(resolve_asset_folder({}, config, only(eq), eq, hs).source == AssetFolderSource::Picker);
}

TEST_CASE("Asset folder: never one Hailstone writes into") {
    TempDir tmp("conflict");
    const fs::path eq = tmp.path / "RoF2";
    fs::create_directories(eq / "hailstone");
    for (bool allow : {false, true}) {
        CAPTURE(allow);
        // The data folder is the asset folder itself: refused, saying why, whatever the build.
        CHECK_FALSE(data_folder_conflict(eq, eq, allow).empty());
        CHECK_FALSE(data_folder_conflict(eq / "", eq / ".", allow).empty());
        // Inside it: refused, unless the build allows a subfolder of Hailstone's own (HAILSTONE_ALLOW_IN_EQ_FOLDER).
        CHECK(data_folder_conflict(eq, eq / "hailstone", allow).empty() == allow);
        CHECK(data_folder_conflict(eq / "", eq / "hailstone" / ".", allow).empty() == allow);
        // Apart, or the asset folder inside the data folder (Hailstone never writes there): fine.
        CHECK(data_folder_conflict(eq, tmp.path / "hailstone", allow).empty());
        CHECK(data_folder_conflict(eq / "hailstone", eq, allow).empty());
        CHECK(data_folder_conflict(tmp.path / "RoF", tmp.path / "RoF2", allow).empty());
        CHECK(data_folder_conflict(eq, {}, allow).empty());
    }

    // A saved assets.path that holds the data folder goes to the picker, saying why (this build's rule).
    Config config;
    config.set("assets.path", eq.string());
    auto r = resolve_asset_folder({}, config, only(eq), eq);
    CHECK(r.source == AssetFolderSource::Picker);
    CHECK(r.reason.find("this same folder") != std::string::npos);
    auto nested = resolve_asset_folder({}, config, only(eq), eq / "hailstone");
    if (data_folder_subfolder_allowed()) {
        CHECK(nested.source == AssetFolderSource::Config);
    } else {
        CHECK(nested.source == AssetFolderSource::Picker);
        CHECK(nested.reason.find("inside this folder") != std::string::npos);
    }
    CHECK(resolve_asset_folder({}, config, only(eq), tmp.path / "data").source == AssetFolderSource::Config);
}

TEST_CASE("Asset folder: a live base needs the server-matched files from the server's assets folder") {
    TempDir base("live"), overlay("overlay");
    auto touch = [](const fs::path& p) {
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "x";
    };
    touch(base.path / "spells_us.txt");
    touch(base.path / "eqstr_us.txt");
    GameDir rof2(base.path, overlay.path);
    CHECK_FALSE(is_live_asset_folder(rof2));
    CHECK(missing_server_files(rof2).empty()); // a RoF2 base has the right ones

    touch(base.path / "spells_us_str.txt"); // only the live client ships it
    touch(overlay.path / "spells_us.txt");
    touch(overlay.path / "RESOURCES" / "skillcaps.txt");
    GameDir live(base.path, overlay.path);
    CHECK(is_live_asset_folder(live));
    CHECK(missing_server_files(live) ==
          std::vector<std::string>{"eqstr_us.txt", "dbstr_us.txt", "Resources/BaseData.txt", "Resources/GlobalLoad.txt"});
}
