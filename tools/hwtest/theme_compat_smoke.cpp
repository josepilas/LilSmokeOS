#include "Theme.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

static bool usableAsset(const std::string &path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) return true;
    fs::path candidate(path);
    if (candidate.extension() == ".svg") {
        for (const char *ext : {".png", ".webp", ".jpg", ".jpeg"}) {
            candidate.replace_extension(ext);
            if (fs::is_regular_file(candidate, ec)) return true;
        }
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: theme_compat_smoke <theme-dir> [...]\n";
        return 2;
    }

    setenv("LILSMOKE_SCREEN_WIDTH", "854", 1);
    setenv("LILSMOKE_SCREEN_HEIGHT", "480", 1);
    bool allOk = true;

    for (int arg = 1; arg < argc; ++arg) {
        const std::string dir = argv[arg];
        lilsmoke::Theme root;
        lilsmoke::Theme snes;
        bool rootOk = root.loadSet(dir);
        bool snesOk = snes.loadSet(dir, "snes", "Super Nintendo");

        int elements = 0;
        int paths = 0;
        int usable = 0;
        int unresolved = 0;
        int duplicateNames = 0;
        for (const char *viewName : {"system", "basic", "detailed", "video", "grid"}) {
            const lilsmoke::ThemeView *view = snes.findView(viewName);
            if (!view) continue;
            elements += static_cast<int>(view->elements.size());
            std::unordered_set<std::string> names;
            for (const auto &element : view->elements) {
                if (!element.name.empty() && !names.insert(element.name).second)
                    duplicateNames++;
                std::string path = element.get("path");
                if (path.empty()) continue;
                paths++;
                if (path.find("${") != std::string::npos) unresolved++;
                if (usableAsset(path)) usable++;
            }
        }

        bool ok = rootOk && snesOk && snes.findView("system") &&
                  (snes.findView("grid") || snes.findView("detailed") ||
                   snes.findView("video") || snes.findView("basic")) &&
                  snes.variable("system.theme") == "snes" &&
                  elements > 0 && paths > 0 && usable > 0 && unresolved == 0 &&
                  duplicateNames == 0;
        allOk = allOk && ok;
        const lilsmoke::ThemeView *systemView = snes.findView("system");
        const lilsmoke::ThemeElement *background =
            systemView ? systemView->find("image", "background") : nullptr;
        const lilsmoke::ThemeElement *systemLogo =
            systemView ? systemView->find("image", "logo") : nullptr;
        const lilsmoke::ThemeElement *systemCarousel =
            systemView ? systemView->find("carousel", "systemcarousel") : nullptr;
        const lilsmoke::ThemeView *videoView = snes.findView("video");
        const lilsmoke::ThemeElement *gameList =
            videoView ? videoView->find("textlist", "gamelist") : nullptr;
        std::cout << (ok ? "PASS" : "FAIL")
                  << " theme=" << fs::path(dir).filename().string()
                  << " elements=" << elements
                  << " paths=" << paths
                  << " usable_assets=" << usable
                  << " unresolved=" << unresolved
                  << " duplicate_names=" << duplicateNames
                  << " system_bg="
                  << (background ? background->get("color", "none") : "missing")
                  << " system_logo="
                  << (systemLogo ? systemLogo->get("path", "<text>") : "missing")
                  << " carousel_count="
                  << (systemCarousel ? systemCarousel->get("maxlogocount", "<default>")
                                     : "missing")
                  << " subset_system="
                  << snes.variable("subset.system-view-style")
                  << " gamelist_pos="
                  << (gameList ? gameList->get("pos", "<default>") : "missing")
                  << " gamelist_origin="
                  << (gameList ? gameList->get("origin", "<default>") : "missing")
                  << " gamelist_size="
                  << (gameList ? gameList->get("size", "<default>") : "missing")
                  << " gamelist_colors="
                  << (gameList ? gameList->get("primarycolor", "<default>") : "missing")
                  << "/"
                  << (gameList ? gameList->get("selectedcolor", "<default>") : "missing")
                  << " gamelist_font="
                  << (gameList ? gameList->get("fontpath", "<default>") : "missing")
                  << " gamelist_visible="
                  << (gameList ? gameList->get("visible", "<default>") : "missing")
                  << "\n";
    }

    return allOk ? 0 : 1;
}
