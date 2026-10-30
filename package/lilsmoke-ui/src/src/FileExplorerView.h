// LilSmoke Explorer - lightweight file manager for LilSmokeUI.

#pragma once

#include "View.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace lilsmoke {

class FileExplorerView : public View {
public:
    explicit FileExplorerView(std::string startPath = "/storage");

    void onEnter() override;
    bool allowAutoBackPop() const override { return false; }
    View *handleInput(InputButton btn, InputEvent evt) override;
    View *handlePointer(const PointerEvent &event) override;
    bool acceptsText() const override { return mode_ == Mode::Prompt; }
    std::string textTitle() const override { return promptTitle_; }
    std::string textValue() const override { return promptValue_; }
    void handleText(const std::string &text) override;
    CursorKind cursorAt(int x, int y) const override;
    void render(Renderer &r, Font &font, const Theme &theme) override;

    void setOnBack(std::function<View *()> cb) { onBack_ = std::move(cb); }
    void focusPath(const std::string &path);
    void setOnPackageApplied(std::function<void()> cb) { onPackageApplied_ = std::move(cb); }
    void setOnApplicationPackage(std::function<void(const std::string &)> cb) { onApplicationPackage_=std::move(cb); }

private:
    enum class Mode {
        Browser,
        Menu,
        Prompt,
        Confirm,
        TextView,
        TextMenu,
        ImageView,
        Message,
    };

    enum class PromptAction {
        None,
        Rename,
        NewFile,
        NewDir,
        EditLine,
        Search,
    };

    enum class ConfirmAction {
        None,
        Delete,
        OverwritePaste,
        ApplyPackage,
    };

    enum class MenuAction {
        Open,
        ToggleSelect,
        Copy,
        Cut,
        Paste,
        Rename,
        Delete,
        NewFile,
        NewDir,
        ExtractHere,
        ExtractToFolder,
        CheckPackage,
        StagePackage,
        ApplyPackage,
        ViewText,
        EditText,
        Properties,
        Refresh,
        GoStorage,
        GoPersist,
        GoRoot,
        SaveText,
        InsertLine,
        DeleteLine,
        Close,
        Cancel,
    };

    struct Entry {
        std::string name;
        std::string path;
        bool isDir = false;
        bool isArchive = false;
        bool isImage = false;
        bool isText = false;
        long long size = 0;
        unsigned long mtime = 0;
    };

    struct MenuItem {
        std::string label;
        MenuAction action;
    };

    std::function<View *()> onBack_;
    std::function<void()> onPackageApplied_;
    std::function<void(const std::string &)> onApplicationPackage_;
    Mode mode_ = Mode::Browser;
    Mode returnMode_ = Mode::Browser;
    std::string currentPath_;
    std::vector<Entry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
    bool dirty_ = true;
    std::string status_;

    std::set<std::string> marked_;
    std::vector<std::string> clipboard_;
    bool clipboardCut_ = false;

    std::vector<MenuItem> menu_;
    int menuSelected_ = 0;
    int menuScroll_ = 0;

    PromptAction promptAction_ = PromptAction::None;
    std::string promptTitle_;
    std::string promptValue_;
    size_t promptCursor_ = 0;

    ConfirmAction confirmAction_ = ConfirmAction::None;
    std::string confirmTitle_;

    std::string viewerPath_;
    std::vector<std::string> textLines_;
    int textTop_ = 0;
    int textLine_ = 0;
    bool textEditable_ = false;
    bool textDirty_ = false;

    std::string imagePath_;
    bool imageFit_ = true;
    int renderWidth_ = 640;
    int renderHeight_ = 480;

    void refresh();
    void openCurrent();
    void openEntry(const Entry &entry);
    void openParent();
    void openContextMenu();
    void openTextMenu();
    void runMenuAction(MenuAction action);
    void startPrompt(PromptAction action, const std::string &title, const std::string &value);
    void finishPrompt();
    void startConfirm(ConfirmAction action, const std::string &title);
    void finishConfirm();
    void showMessage(const std::string &message, Mode returnMode = Mode::Browser);

    Entry *currentEntry();
    const Entry *currentEntry() const;
    std::vector<std::string> activePaths() const;

    bool loadText(const std::string &path, bool editable);
    bool saveText();
    bool openArchiveListing(const std::string &path);
    bool extractArchive(const std::string &path, const std::string &destDir);
    void runSystemPackage(const std::string &path, const char *action);
    bool pasteClipboard(bool overwrite);
    bool deletePaths(const std::vector<std::string> &paths);
    bool renameCurrent(const std::string &newName);
    bool createFile(const std::string &name);
    bool createDir(const std::string &name);
    std::string propertiesFor(const std::string &path) const;
};

}  // namespace lilsmoke
