// LilSmokeUI - low-memory game handoff.

#include "Launcher.h"
#include "Log.h"
#include "ProcessLaunch.h"
#include "EmulatorRegistry.h"
#include <sys/stat.h>

namespace lilsmoke {

static std::string parentFolderName(const std::string &path) {
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash == std::string::npos) return "";
    size_t parentEnd = lastSlash;
    while (parentEnd > 0 && path[parentEnd - 1] == '/') parentEnd--;
    size_t parentStart = path.find_last_of('/', parentEnd == 0 ? 0 : parentEnd - 1);
    if (parentStart == std::string::npos) return path.substr(0, parentEnd);
    return path.substr(parentStart + 1, parentEnd - parentStart - 1);
}

Launcher::Launcher(const ROM &rom) : rom_(rom) {}

void Launcher::onEnter() {
    if (launched_) return;
    launched_ = true;

    struct stat st{};
    if (stat(rom_.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        errorMsg_ = "Game file is missing";
        return;
    }
    const char *session = "/usr/lib/lilsmoke/lilsmoke-session";
    if (access(session, X_OK) != 0) {
        errorMsg_ = "Session helper unavailable";
        return;
    }

    LOG_INFO("Launcher: starting %s [%s]", rom_.path.c_str(), rom_.system.c_str());

    // main destroys the UI's SDL/DirectFB context before replacing the process.
    if (rom_.system == "portsmoke") {
        std::string gameFolder = parentFolderName(rom_.path);
        requestProcessLaunch({session, {"lilsmoke-session", rom_.system, rom_.path, rom_.name,
            "/usr/bin/portsmoke-launch", "portsmoke-launch", gameFolder}});
    } else {
        const auto config = readEmulatorConfig("/persist/config/lilsmoke.conf");
        const auto providers = emulatorProviders();
        const auto id = selectedEmulator(rom_.system, config, providers);
        const auto provider = std::find_if(providers.begin(), providers.end(),
            [&id](const auto &p) { return p.id == id; });
        if (provider == providers.end() || !provider->available() || !provider->supports(rom_.system)) {
            errorMsg_ = "No compatible installed engine selected for " + emulatorSystemName(rom_.system);
            return;
        }
        requestProcessLaunch({session, {"lilsmoke-session", rom_.system, rom_.path, rom_.name,
            "/usr/lib/lilsmoke/lilsmoke-launch", "lilsmoke-launch", rom_.system, rom_.path}});
    }
}

View *Launcher::handleInput(InputButton btn, InputEvent evt) {
    if (evt == InputEvent::Pressed && btn == InputButton::B && !errorMsg_.empty()) {
        return nullptr;
    }
    return nullptr;
}

void Launcher::render(Renderer &r, Font &font, const Theme &) {
    r.clear(0x2B2D31FF);

    if (errorMsg_.empty()) {
        font.render(r, "Launching " + rom_.name + "...", 16,
                    r.height() / 2 - 8, 0xD9C9A3FF);
    } else {
        font.render(r, "Launch failed:", 16, 48, 0xFF8080FF);
        font.renderStyled(r, errorMsg_, 16, 84, r.width() - 32,
                          r.height() - 132, 0xD9C9A3FF, "", 16, "left", "top", true);
        font.renderStyled(r, rom_.name, 16, r.height() - 32, r.width() - 32,
                          24, 0xD9C9A399, "", 14);
    }
}

}  // namespace lilsmoke
