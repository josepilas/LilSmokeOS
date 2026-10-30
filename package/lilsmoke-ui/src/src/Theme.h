// LilSmokeUI - Theme.h
//
// EmulationStation theme loader for the EmuELEC/ArkOS theme model.
// It supports default theme-set XML, per-system overrides, include files,
// variables, normalized layout properties, and the ES element names that
// LilSmokeUI can render today.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lilsmoke {

struct ThemeAnimation {
    enum class Property : uint8_t { X, Y, Scale, Opacity, Rotation };
    enum class Mode : uint8_t { Linear, EaseIn, EaseOut, EaseInOut };

    static constexpr uint32_t MaxTracks = 32;
    static constexpr uint32_t MaxTimeMs = 60000;
    static constexpr uint32_t MaxRepeat = 32;
    static constexpr uint32_t MaxEventLength = 64;
    static constexpr uint32_t MaxStoryboardMs = MaxTimeMs + MaxTimeMs * MaxRepeat * 2;

    std::string event;
    Property property = Property::X;
    Mode mode = Mode::Linear;
    float from = 0.0f;
    float to = 0.0f;
    uint32_t beginMs = 0;
    uint32_t durationMs = 0;
    uint32_t repeat = 1;  // Total iterations, not additional iterations.
    uint32_t storyboardDurationMs = 0;
    uint32_t storyboardRepeat = 1;
    bool hasFrom = false;
    bool hasTo = false;
    bool autoReverse = false;
};

struct ThemeElement {
    std::string name;
    std::string type;
    std::unordered_map<std::string, std::string> props;
    std::vector<ThemeAnimation> animations;
    // Bounded merge metadata also records empty storyboards that clear an event.
    std::vector<std::string> storyboardEvents;

    std::string get(const std::string &k, const std::string &def = "") const {
        auto it = props.find(k);
        return it == props.end() ? def : it->second;
    }

    void getPos(float &x, float &y, float defX = 0, float defY = 0) const;
    void getSize(float &w, float &h, float defW = 1, float defH = 1) const;
    void getOrigin(float &x, float &y, float defX = 0, float defY = 0) const;
    float getFloat(const std::string &key, float def = 0.0f) const;
    // Literal-key lookup avoids temporary strings and exception allocation.
    float getFloat(const char *key, float def = 0.0f) const;
    bool getBool(const std::string &key, bool def = false) const;
    uint32_t getColor(const std::string &key, uint32_t def = 0xFFFFFFFF) const;
};

struct ThemeView {
    std::string name;
    std::vector<ThemeElement> elements;

    const ThemeElement *find(const std::string &type,
                             const std::string &name) const;
};

class Theme {
public:
    Theme();

    bool load(const std::string &themeXmlPath,
              const std::string &systemTheme = "",
              const std::string &systemName = "");

    bool loadSet(const std::string &themeSetDir,
                 const std::string &systemTheme = "",
                 const std::string &systemName = "");

    const ThemeView *findView(const std::string &name) const;
    std::string variable(const std::string &name) const;

    bool loaded() const { return loaded_; }
    uint64_t revision() const { return revision_; }
    const std::string &dir() const { return dir_; }
    const std::string &rootDir() const { return rootDir_; }
    const std::string &defaultView() const { return defaultView_; }
    const std::string &defaultTransition() const { return defaultTransition_; }

private:
    uint64_t revision_ = 0;
    bool loaded_ = false;
    std::string dir_;
    std::string rootDir_;
    std::string defaultView_;
    std::string defaultTransition_;
    std::unordered_map<std::string, std::string> variables_;
    std::vector<ThemeView> views_;

    void seedVariables(const std::string &rootDir,
                       const std::string &systemTheme,
                       const std::string &systemName);

    friend class ThemeParser;
};

}  // namespace lilsmoke
