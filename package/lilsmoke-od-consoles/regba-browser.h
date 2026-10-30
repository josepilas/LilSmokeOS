/* Native ReGBA browser extension, GPL-2.0-or-later. */
#include <dirent.h>
#include "od-content.h"

static int od_gba_entry(const struct dirent *entry)
{
    const char *extension;
    struct stat st;
    if (!strcmp(entry->d_name, "..")) return 1;
    if (entry->d_name[0] == '.') return 0;
    if (!stat(entry->d_name, &st) && S_ISDIR(st.st_mode)) return 1;
    extension = strrchr(entry->d_name, '.');
    return extension && (!strcasecmp(extension, ".gba") || !strcasecmp(extension, ".agb") ||
                         od_is_archive(entry->d_name));
}

static int od_gba_browser(void)
{
    char previous[PATH_MAX], directory[PATH_MAX], filename[PATH_MAX];
    struct dirent **entries = NULL;
    int count, index = 0, reload = 1, i, loaded = 0;
    getcwd(previous, sizeof(previous));
    snprintf(directory, sizeof(directory), "%s", od_rom_directory("gba"));
    while (1) {
        enum GUI_Action action;
        if (reload) {
            for (i = 0; entries && i < count; ++i) free(entries[i]);
            free(entries); entries = NULL;
            count = chdir(directory) ? -1 : scandir(".", &entries, od_gba_entry, alphasort);
            index = 0; reload = 0;
        }
        SDL_FillRect(OutputSurface, NULL, COLOR_BACKGROUND);
        PrintString(directory, COLOR_TITLE_TEXT, OutputSurface->pixels, OutputSurface->pitch,
                    4, 2, 312, 22, LEFT, TOP);
        if (count < 0)
            PrintString("ROM directory unavailable", COLOR_ERROR_TEXT, OutputSurface->pixels,
                        OutputSurface->pitch, 4, 30, 312, 22, LEFT, TOP);
        for (i = index / 9 * 9; i < count && i < index / 9 * 9 + 9; ++i)
            PrintString(entries[i]->d_name, i == index ? COLOR_ACTIVE_TEXT : COLOR_INACTIVE_TEXT,
                        OutputSurface->pixels, OutputSurface->pitch, 4, 28 + (i % 9) * 22,
                        312, 22, LEFT, TOP);
        ReGBA_VideoFlip();
        action = GetGUIAction();
        if (action == GUI_ACTION_LEAVE) break;
        if (action == GUI_ACTION_DOWN && count > 0) index = (index + 1) % count;
        if (action == GUI_ACTION_UP && count > 0) index = (index + count - 1) % count;
        if (action == GUI_ACTION_ENTER && count > 0) {
            struct stat st;
            snprintf(filename, sizeof(filename), "%s/%s", directory, entries[index]->d_name);
            if (!stat(filename, &st) && S_ISDIR(st.st_mode)) {
                if (realpath(filename, directory)) reload = 1;
            } else {
                char bios[PATH_MAX];
                const char *bios_directory = getenv("BIOS_DIR");
                snprintf(bios, sizeof(bios), "%s/gba_bios.bin", bios_directory ? bios_directory : "/storage/bios");
                if (load_bios(bios) == -1) {
                    snprintf(bios, sizeof(bios), "%s/gba_bios.bin", main_path);
                    if (load_bios(bios) == -1 && load_bios("./gba_bios.bin") == -1) {
                        ShowErrorScreen("User GBA BIOS required: %s", bios);
                        continue;
                    }
                }
                if (load_gamepak(filename) == -1) {
                    ShowErrorScreen("Cannot load ROM: %s", filename);
                    continue;
                }
                char name[MAX_PATH + 1];
                GetFileNameNoExtension(name, CurrentGamePath);
                ReGBA_LoadSettings(name, true);
                init_cpu(!ResolveSetting(BootFromBIOS, PerGameBootFromBIOS));
                loaded = 1; break;
            }
        }
        SDL_Delay(10);
    }
    for (i = 0; entries && i < count; ++i) free(entries[i]);
    free(entries);
    chdir(previous);
    return loaded;
}

static void ActionLoadROM(struct Menu **menu, uint32_t *index)
{
    (void)index;
    if (od_gba_browser()) *menu = NULL;
}
