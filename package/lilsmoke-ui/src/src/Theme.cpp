// LilSmokeUI - Theme.cpp

#include "Theme.h"
#include "ThemeAnimation.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>

namespace lilsmoke {

namespace {

struct XmlToken {
    enum Type { OpenTag, CloseTag, SelfClose, Text } type;
    std::string name;
    std::string text;
    std::unordered_map<std::string, std::string> attrs;
};

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static bool dirExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool isAbsolutePath(const std::string &path) {
    if (path.empty()) return false;
    if (path[0] == '/') return true;
    return path.size() > 2 && std::isalpha((unsigned char)path[0]) &&
           path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

static std::string dirnameOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

static std::string joinPath(const std::string &base, const std::string &path) {
    if (path.empty() || isAbsolutePath(path)) return path;
    if (path.compare(0, 2, "~/") == 0) return path;
    if (base.empty() || base == ".") return path;
    char last = base[base.size() - 1];
    if (last == '/' || last == '\\') return base + path;
    return base + "/" + path;
}

static bool readFile(const std::string &path, std::string &out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static bool pathLikeKey(const std::string &key) {
    if (key == "path" || key == "default" || key == "sound" ||
        key == "active" || key == "inactive" || key == "incharge" ||
        key == "full" || key == "at75" || key == "at50" ||
        key == "at25" || key == "empty" || key == "networkicon") {
        return true;
    }
    if (key.size() >= 4 && key.compare(key.size() - 4, 4, "path") == 0)
        return true;
    if (key.size() >= 5 && key.compare(key.size() - 5, 5, "sound") == 0)
        return true;
    return key.size() > 4 && key.compare(0, 4, "icon") == 0;
}

static std::vector<std::string> splitNames(const std::string &value) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : value) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

class XmlLexer {
public:
    explicit XmlLexer(const std::string &src) : src_(src) {}

    bool next(XmlToken &out) {
        out = XmlToken{};

        while (true) {
            if (pos_ >= src_.size()) return false;

            if (src_[pos_] != '<') {
                size_t end = src_.find('<', pos_);
                if (end == std::string::npos) end = src_.size();
                out.type = XmlToken::Text;
                out.text = src_.substr(pos_, end - pos_);
                pos_ = end;
                return true;
            }

            if (startsWith("<!--")) {
                size_t end = src_.find("-->", pos_ + 4);
                pos_ = end == std::string::npos ? src_.size() : end + 3;
                continue;
            }

            if (startsWith("<?")) {
                size_t end = src_.find("?>", pos_ + 2);
                pos_ = end == std::string::npos ? src_.size() : end + 2;
                continue;
            }

            if (startsWith("<![CDATA[")) {
                size_t start = pos_ + 9;
                size_t end = src_.find("]]>", start);
                out.type = XmlToken::Text;
                out.text = src_.substr(start, end == std::string::npos
                                                ? std::string::npos
                                                : end - start);
                pos_ = end == std::string::npos ? src_.size() : end + 3;
                return true;
            }

            if (startsWith("<!")) {
                size_t end = src_.find('>', pos_ + 2);
                pos_ = end == std::string::npos ? src_.size() : end + 1;
                continue;
            }

            break;
        }

        pos_++;
        if (pos_ < src_.size() && src_[pos_] == '/') {
            pos_++;
            size_t end = src_.find('>', pos_);
            if (end == std::string::npos) return false;
            out.type = XmlToken::CloseTag;
            out.name = lower(trim(src_.substr(pos_, end - pos_)));
            pos_ = end + 1;
            return true;
        }

        size_t end = findTagEnd(pos_);
        if (end == std::string::npos) return false;

        std::string tag = trim(src_.substr(pos_, end - pos_));
        pos_ = end + 1;

        bool selfClose = !tag.empty() && tag[tag.size() - 1] == '/';
        if (selfClose) tag = trim(tag.substr(0, tag.size() - 1));

        size_t i = 0;
        while (i < tag.size() && !std::isspace((unsigned char)tag[i])) i++;
        out.name = lower(tag.substr(0, i));
        out.attrs.clear();

        while (i < tag.size()) {
            while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
            if (i >= tag.size()) break;

            size_t nameStart = i;
            while (i < tag.size() && tag[i] != '=' &&
                   !std::isspace((unsigned char)tag[i])) {
                i++;
            }
            std::string attrName = lower(tag.substr(nameStart, i - nameStart));

            while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
            std::string attrValue = "true";
            if (i < tag.size() && tag[i] == '=') {
                i++;
                while (i < tag.size() && std::isspace((unsigned char)tag[i])) i++;
                if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
                    char quote = tag[i++];
                    size_t valueStart = i;
                    while (i < tag.size() && tag[i] != quote) i++;
                    attrValue = tag.substr(valueStart, i - valueStart);
                    if (i < tag.size()) i++;
                } else {
                    size_t valueStart = i;
                    while (i < tag.size() &&
                           !std::isspace((unsigned char)tag[i])) {
                        i++;
                    }
                    attrValue = tag.substr(valueStart, i - valueStart);
                }
            }
            if (!attrName.empty()) out.attrs[attrName] = attrValue;
        }

        out.type = selfClose ? XmlToken::SelfClose : XmlToken::OpenTag;
        return true;
    }

private:
    bool startsWith(const char *prefix) const {
        size_t n = std::char_traits<char>::length(prefix);
        return src_.compare(pos_, n, prefix) == 0;
    }

    size_t findTagEnd(size_t start) const {
        char quote = 0;
        for (size_t i = start; i < src_.size(); i++) {
            char c = src_[i];
            if (quote) {
                if (c == quote) quote = 0;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c == '>') return i;
        }
        return std::string::npos;
    }

    const std::string &src_;
    size_t pos_ = 0;
};

static void parsePair(const std::string &s,
                      float &x,
                      float &y,
                      float defX,
                      float defY) {
    x = defX;
    y = defY;
    std::istringstream ss(s);
    ss >> x;
    if (!(ss >> y)) y = x;
}

}  // namespace

class ThemeParser {
public:
    ThemeParser(Theme &theme,
                std::string systemTheme,
                std::string systemName)
        : theme_(theme), systemTheme_(std::move(systemTheme)) {
        (void)systemName;
        seedConfiguredSubsets();
    }

    bool parseFile(const std::string &path, int depth = 0) {
        if (depth > 16) {
            LOG_WARN("Theme: include depth exceeded at %s", path.c_str());
            return false;
        }

        std::string src;
        if (!readFile(path, src)) {
            LOG_WARN("Theme: cannot open %s", path.c_str());
            return false;
        }

        // EmulationStation themes may declare subsets and variables after
        // includes which already depend on them. Resolve those declarations
        // first, then perform the normal ordered merge pass.
        prescanSubsets(src);
        prescanVariables(src);

        std::string currentDir = dirnameOf(path);
        theme_.dir_ = currentDir;
        XmlLexer lexer(src);
        XmlToken tok;
        bool foundTheme = false;

        while (lexer.next(tok)) {
            if (tok.type == XmlToken::OpenTag && tok.name == "theme") {
                foundTheme = true;
                auto defaultView = tok.attrs.find("defaultview");
                if (defaultView != tok.attrs.end())
                    theme_.defaultView_ = substitute(defaultView->second);
                auto defaultTransition = tok.attrs.find("defaulttransition");
                if (defaultTransition != tok.attrs.end())
                    theme_.defaultTransition_ = substitute(defaultTransition->second);
                break;
            }
        }
        if (!foundTheme) {
            LOG_WARN("Theme: %s has no <theme> root", path.c_str());
            return false;
        }

        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == "theme") break;
            if (tok.type != XmlToken::OpenTag &&
                tok.type != XmlToken::SelfClose) {
                continue;
            }

            if (tok.name == "include") {
                std::string includePath;
                if (tok.type != XmlToken::SelfClose) {
                    includePath = readTextElement(lexer, tok.name);
                }
                if (!conditionsMatch(tok.attrs)) continue;
                includePath = substitute(trim(includePath));
                if (!includePath.empty()) {
                    includePath = joinPath(currentDir, includePath);
                    auto subset = tok.attrs.find("subset");
                    if (subset != tok.attrs.end()) {
                        std::string subsetName = lower(subset->second);
                        auto nameIt = tok.attrs.find("name");
                        std::string choice = lower(nameIt == tok.attrs.end()
                                                       ? includePath
                                                       : nameIt->second);
                        auto active = activeSubsets_.find(subsetName);
                        if (active == activeSubsets_.end()) {
                            activeSubsets_[subsetName] = choice;
                            theme_.variables_["subset." + subsetName] = choice;
                        } else if (active->second != choice) {
                            continue;
                        }
                    }
                    parseFile(includePath, depth + 1);
                }
            } else if (tok.name == "subset") {
                if (tok.type != XmlToken::SelfClose)
                    parseSubset(lexer, tok, currentDir, depth);
            } else if (tok.name == "variables") {
                if (!conditionsMatch(tok.attrs)) {
                    if (tok.type != XmlToken::SelfClose)
                        skipElement(lexer, tok.name);
                } else if (tok.type != XmlToken::SelfClose) {
                    parseVariables(lexer);
                }
            } else if (tok.name == "view" || tok.name == "customview") {
                if (!conditionsMatch(tok.attrs)) {
                    if (tok.type != XmlToken::SelfClose)
                        skipElement(lexer, tok.name);
                } else if (tok.type != XmlToken::SelfClose) {
                    parseView(lexer, tok, currentDir);
                }
            } else if (tok.name == "feature") {
                // ArkOS/EmuELEC themes commonly wrap carousel-aware views in
                // <feature supported="carousel">. LilSmokeUI ignores unknown
                // widgets, but the nested view geometry is still useful.
                if (tok.type != XmlToken::SelfClose)
                    parseFeature(lexer, currentDir);
            } else {
                if (tok.type != XmlToken::SelfClose)
                    skipElement(lexer, tok.name);
            }
        }

        return true;
    }

private:
    void seedConfiguredSubsets() {
        const char *configured = std::getenv("LILSMOKE_THEME_SUBSETS");
        if (!configured || !*configured) return;

        std::string values(configured);
        size_t begin = 0;
        while (begin <= values.size()) {
            size_t end = values.find(';', begin);
            std::string item = trim(values.substr(
                begin, end == std::string::npos ? std::string::npos
                                                : end - begin));
            size_t separator = item.find('=');
            if (separator != std::string::npos) {
                std::string name = lower(trim(item.substr(0, separator)));
                std::string value = lower(trim(item.substr(separator + 1)));
                if (!name.empty() && !value.empty()) {
                    activeSubsets_[name] = value;
                    theme_.variables_["subset." + name] = value;
                }
            }
            if (end == std::string::npos) break;
            begin = end + 1;
        }
    }

    static bool valueIsTrue(const std::string &value) {
        std::string normalized = lower(trim(value));
        return normalized == "true" || normalized == "1" || normalized == "yes";
    }

    bool subsetMatches(const std::string &condition) const {
        size_t begin = 0;
        while (begin <= condition.size()) {
            size_t comma = condition.find(',', begin);
            std::string clause = trim(condition.substr(
                begin, comma == std::string::npos ? std::string::npos : comma - begin));
            size_t colon = clause.find(':');
            if (colon == std::string::npos) return false;

            std::string subset = lower(trim(clause.substr(0, colon)));
            std::string accepted = lower(trim(clause.substr(colon + 1)));
            auto active = activeSubsets_.find(subset);
            if (active == activeSubsets_.end()) return false;

            bool matched = false;
            size_t valueBegin = 0;
            while (valueBegin <= accepted.size()) {
                size_t pipe = accepted.find('|', valueBegin);
                std::string value = trim(accepted.substr(
                    valueBegin,
                    pipe == std::string::npos ? std::string::npos : pipe - valueBegin));
                if (active->second == value) matched = true;
                if (pipe == std::string::npos) break;
                valueBegin = pipe + 1;
            }
            if (!matched) return false;
            if (comma == std::string::npos) break;
            begin = comma + 1;
        }
        return true;
    }

    bool conditionsMatch(
        const std::unordered_map<std::string, std::string> &attrs) const {
        auto subset = attrs.find("ifsubset");
        if (subset != attrs.end() && !subsetMatches(substitute(subset->second)))
            return false;

        auto help = attrs.find("ifhelpprompts");
        if (help != attrs.end()) {
            const char *configured = std::getenv("LILSMOKE_SHOW_HELP_PROMPTS");
            bool enabled = !configured || valueIsTrue(configured);
            if (valueIsTrue(help->second) != enabled) return false;
        }

        auto tiny = attrs.find("tinyscreen");
        if (tiny != attrs.end()) {
            int width = std::atoi(theme_.variables_.at("screen.width").c_str());
            int height = std::atoi(theme_.variables_.at("screen.height").c_str());
            const bool smallScreen = width < 700 || height < 700;
            if (valueIsTrue(tiny->second) != smallScreen) return false;
        }
        return true;
    }

    static double ratioFromChoice(const std::string &choice) {
        size_t sep = choice.find_first_of("-:");
        if (sep == std::string::npos) return 0.0;
        char *end = nullptr;
        const std::string numeratorText = choice.substr(0, sep);
        const std::string denominatorText = choice.substr(sep + 1);
        double numerator = std::strtod(numeratorText.c_str(), &end);
        if (end == numeratorText.c_str() || *end != '\0' ||
            !std::isfinite(numerator) || numerator <= 0.0) return 0.0;
        double denominator = std::strtod(denominatorText.c_str(), &end);
        if (end == denominatorText.c_str() || *end != '\0' ||
            !std::isfinite(denominator) || denominator <= 0.0) return 0.0;
        return numerator / denominator;
    }

    void prescanSubsets(const std::string &src) {
        XmlLexer lexer(src);
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type != XmlToken::OpenTag || tok.name != "subset") continue;
            auto nameIt = tok.attrs.find("name");
            if (nameIt == tok.attrs.end()) {
                skipElement(lexer, tok.name);
                continue;
            }

            std::string subsetName = lower(substitute(nameIt->second));
            std::vector<std::string> choices;
            int nested = 1;
            while (nested > 0 && lexer.next(tok)) {
                if (tok.type == XmlToken::OpenTag && tok.name == "subset") {
                    nested++;
                } else if (tok.type == XmlToken::CloseTag && tok.name == "subset") {
                    nested--;
                } else if (nested == 1 &&
                           (tok.type == XmlToken::OpenTag ||
                            tok.type == XmlToken::SelfClose) &&
                           tok.name == "include") {
                    auto choiceIt = tok.attrs.find("name");
                    if (choiceIt != tok.attrs.end())
                        choices.push_back(lower(substitute(choiceIt->second)));
                }
            }

            if (choices.empty()) continue;

            auto configured = activeSubsets_.find(subsetName);
            if (configured != activeSubsets_.end()) {
                if (std::find(choices.begin(), choices.end(), configured->second) !=
                    choices.end()) {
                    continue;
                }
                // A global preference may not exist in every theme. Falling
                // back to that theme's first choice mirrors ES behavior and
                // avoids dropping the whole subset include.
                activeSubsets_.erase(configured);
            }

            std::string selected = choices.front();
            if (subsetName.find("aspect") != std::string::npos) {
                double screenRatio = std::strtod(
                    theme_.variables_["screen.ratio"].c_str(), nullptr);
                double bestDistance = 1000.0;
                for (const auto &choice : choices) {
                    double ratio = ratioFromChoice(choice);
                    if (ratio <= 0.0) continue;
                    double distance = std::fabs(ratio - screenRatio);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        selected = choice;
                    }
                }
            }
            activeSubsets_[subsetName] = selected;
            theme_.variables_["subset." + subsetName] = selected;
        }
    }

    void prescanVariables(const std::string &src) {
        XmlLexer lexer(src);
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type != XmlToken::OpenTag || tok.name != "variables") continue;
            if (!conditionsMatch(tok.attrs)) {
                skipElement(lexer, tok.name);
                continue;
            }
            parseVariables(lexer);
        }
    }

    void parseSubset(XmlLexer &lexer,
                     const XmlToken &subsetTok,
                     const std::string &currentDir,
                     int depth) {
        auto nameIt = subsetTok.attrs.find("name");
        if (nameIt == subsetTok.attrs.end()) {
            skipElement(lexer, subsetTok.name);
            return;
        }
        std::string subsetName = lower(substitute(nameIt->second));
        auto selected = activeSubsets_.find(subsetName);

        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == "subset") break;
            if ((tok.type != XmlToken::OpenTag && tok.type != XmlToken::SelfClose) ||
                tok.name != "include") {
                if (tok.type == XmlToken::OpenTag) skipElement(lexer, tok.name);
                continue;
            }

            std::string includePath;
            if (tok.type != XmlToken::SelfClose)
                includePath = readTextElement(lexer, tok.name);
            auto choiceIt = tok.attrs.find("name");
            std::string choice = lower(choiceIt == tok.attrs.end()
                                           ? includePath
                                           : substitute(choiceIt->second));
            if (selected == activeSubsets_.end() || selected->second != choice)
                continue;
            includePath = substitute(trim(includePath));
            if (!includePath.empty())
                parseFile(joinPath(currentDir, includePath), depth + 1);
        }
    }

    std::string substitute(const std::string &value) const {
        std::string out;
        size_t pos = 0;
        while (pos < value.size()) {
            size_t start = value.find("${", pos);
            if (start == std::string::npos) {
                out += value.substr(pos);
                break;
            }
            out += value.substr(pos, start - pos);
            size_t end = value.find('}', start + 2);
            if (end == std::string::npos) {
                out += value.substr(start);
                break;
            }
            std::string key = value.substr(start + 2, end - start - 2);
            auto it = theme_.variables_.find(key);
            if (it == theme_.variables_.end())
                it = theme_.variables_.find(lower(key));
            if (it != theme_.variables_.end())
                out += it->second;
            pos = end + 1;
        }

        // Older ArkOS themes also use the legacy $system token.
        const std::string system = theme_.variable("system.theme");
        if (!system.empty()) {
            size_t legacy = 0;
            while ((legacy = out.find("$system", legacy)) != std::string::npos) {
                if (legacy + 7 < out.size() && out[legacy + 7] == '{') {
                    legacy += 7;
                    continue;
                }
                out.replace(legacy, 7, system);
                legacy += system.size();
            }
        }
        return out;
    }

    std::string readTextElement(XmlLexer &lexer, const std::string &tagName) {
        std::string text;
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == tagName) break;
            if (tok.type == XmlToken::Text) {
                text += tok.text;
            } else if (tok.type == XmlToken::OpenTag) {
                skipElement(lexer, tok.name);
            }
        }
        return trim(text);
    }

    void skipElement(XmlLexer &lexer, const std::string &tagName) {
        int depth = 1;
        XmlToken tok;
        while (depth > 0 && lexer.next(tok)) {
            if (tok.type == XmlToken::OpenTag && tok.name == tagName) depth++;
            else if (tok.type == XmlToken::CloseTag && tok.name == tagName) depth--;
        }
    }

    void parseVariables(XmlLexer &lexer) {
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == "variables") break;
            if (tok.type != XmlToken::OpenTag &&
                tok.type != XmlToken::SelfClose) {
                continue;
            }

            std::string key = tok.name;
            std::string value;
            if (tok.type != XmlToken::SelfClose)
                value = readTextElement(lexer, tok.name);
            value = substitute(trim(value));
            theme_.variables_[key] = value;
        }
    }

    void parseView(XmlLexer &lexer,
                   const XmlToken &viewTok,
                   const std::string &currentDir) {
        auto nameIt = viewTok.attrs.find("name");
        if (nameIt == viewTok.attrs.end()) {
            skipElement(lexer, viewTok.name);
            return;
        }

        std::vector<std::string> viewNames = splitNames(nameIt->second);
        if (viewNames.empty()) {
            skipElement(lexer, viewTok.name);
            return;
        }

        std::vector<ThemeElement> elements;
        auto inherits = viewTok.attrs.find("inherits");
        if (inherits != viewTok.attrs.end()) {
            const ThemeView *base = theme_.findView(inherits->second);
            if (base) elements = base->elements;
        }

        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == viewTok.name) break;
            if (tok.type != XmlToken::OpenTag &&
                tok.type != XmlToken::SelfClose) {
                continue;
            }

            ThemeElement elem;
            elem.type = tok.name;
            auto elemName = tok.attrs.find("name");
            if (elemName != tok.attrs.end()) elem.name = elemName->second;

            if (!conditionsMatch(tok.attrs)) {
                if (tok.type != XmlToken::SelfClose)
                    skipElement(lexer, tok.name);
                continue;
            }

            for (const auto &kv : tok.attrs) {
                if (kv.first == "name") continue;
                if (kv.first.compare(0, 2, "if") == 0) continue;
                elem.props[kv.first] = substitute(kv.second);
            }

            if (tok.type != XmlToken::SelfClose) {
                parseElementProperties(lexer, tok.name, currentDir, elem);
            }

            std::vector<std::string> elementNames = splitNames(elem.name);
            if (elementNames.size() <= 1) {
                mergeElement(elements, elem);
            } else {
                // ES applies a comma/space-delimited name declaration to
                // every named element. Expanding here lets later style-only
                // declarations merge with the corresponding concrete item.
                for (const auto &name : elementNames) {
                    ThemeElement expanded = elem;
                    expanded.name = name;
                    mergeElement(elements, expanded);
                }
            }
        }

        for (const auto &name : viewNames) {
            ThemeView &view = ensureView(name);
            for (const auto &elem : elements)
                mergeElement(view.elements, elem);
        }
    }

    void parseFeature(XmlLexer &lexer, const std::string &currentDir) {
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == "feature") break;
            if (tok.type != XmlToken::OpenTag &&
                tok.type != XmlToken::SelfClose) {
                continue;
            }

            if (tok.name == "view" || tok.name == "customview") {
                if (!conditionsMatch(tok.attrs)) {
                    if (tok.type != XmlToken::SelfClose)
                        skipElement(lexer, tok.name);
                } else if (tok.type != XmlToken::SelfClose) {
                    parseView(lexer, tok, currentDir);
                }
            } else if (tok.type != XmlToken::SelfClose) {
                skipElement(lexer, tok.name);
            }
        }
    }

    void parseElementProperties(XmlLexer &lexer,
                                const std::string &elementTag,
                                const std::string &baseDir,
                                ThemeElement &elem) {
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == elementTag) break;
            if (tok.type != XmlToken::OpenTag &&
                tok.type != XmlToken::SelfClose) {
                continue;
            }

            if (tok.name == "storyboard") {
                if (conditionsMatch(tok.attrs)) {
                    parseStoryboard(lexer, tok, elem);
                } else if (tok.type != XmlToken::SelfClose) {
                    skipElement(lexer, tok.name);
                }
                continue;
            }

            std::string key = tok.name;
            std::string value;
            if (tok.type != XmlToken::SelfClose)
                value = readTextElement(lexer, tok.name);
            if (!conditionsMatch(tok.attrs)) continue;
            value = substitute(trim(value));
            if (pathLikeKey(key) && !value.empty()) {
                value = resolveAssetPath(baseDir, value);
                // EmulationStation keeps the previous valid declaration when
                // a later fallback path does not exist.
                if (!usableAssetPath(value)) continue;
            }
            elem.props[key] = value;
        }
    }

    std::string animationAttribute(const XmlToken &tok, const char *key) const {
        auto it = tok.attrs.find(key);
        return it == tok.attrs.end() ? "" : trim(substitute(it->second));
    }

    static bool boundedInteger(const std::string &value, uint32_t limit,
                               uint32_t &out) {
        if (value.empty() || value.size() > 64) return false;
        size_t pos = value[0] == '+' ? 1 : 0;
        if (pos == value.size()) return false;
        out = 0;
        for (; pos < value.size(); ++pos) {
            const char c = value[pos];
            if (c < '0' || c > '9') return false;
            // Saturate before any arithmetic can overflow on the 32-bit target.
            out = std::min(limit, out * 10u + static_cast<uint32_t>(c - '0'));
        }
        return true;
    }

    bool animationRepeat(const XmlToken &tok, uint32_t &out) const {
        const std::string value = lower(animationAttribute(tok, "repeat"));
        out = 1;
        if (value.empty() || value == "none") return true;
        return boundedInteger(value, ThemeAnimation::MaxRepeat, out) && out != 0;
    }

    bool animationTime(const XmlToken &tok, const char *key, uint32_t &out) const {
        out = 0;
        if (tok.attrs.find(key) == tok.attrs.end()) return true;
        return boundedInteger(animationAttribute(tok, key), ThemeAnimation::MaxTimeMs, out);
    }

    bool animationEndpoint(const XmlToken &tok, const char *key,
                           ThemeAnimation::Property property,
                           bool &present, float &out) const {
        present = tok.attrs.find(key) != tok.attrs.end();
        if (!present) return true;
        const std::string value = animationAttribute(tok, key);
        if (value.empty() || value.size() > 64) return false;
        char *end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
        out = themeui::animation_detail::boundedValue(property,
            static_cast<float>(std::max(-36000.0, std::min(36000.0, parsed))));
        return true;
    }

    bool parseAnimation(const XmlToken &tok, ThemeAnimation &track) const {
        using Property = ThemeAnimation::Property;
        using Mode = ThemeAnimation::Mode;
        const std::string property = animationAttribute(tok, "property");
        if (property == "x") track.property = Property::X;
        else if (property == "y") track.property = Property::Y;
        else if (property == "scale") track.property = Property::Scale;
        else if (property == "opacity") track.property = Property::Opacity;
        else if (property == "rotation") track.property = Property::Rotation;
        else return false;

        const std::string enabled = lower(animationAttribute(tok, "enabled"));
        if (!enabled.empty() && enabled != "true" && enabled != "1") return false;
        if (tok.attrs.find("repeatat") != tok.attrs.end()) return false;
        std::string mode = lower(animationAttribute(tok, "mode"));
        if (mode.empty()) mode = lower(animationAttribute(tok, "easingmode"));
        if (mode.empty() || mode == "linear") track.mode = Mode::Linear;
        else if (mode == "easein") track.mode = Mode::EaseIn;
        else if (mode == "easeout") track.mode = Mode::EaseOut;
        else if (mode == "easeinout") track.mode = Mode::EaseInOut;
        else return false;

        const std::string reverse = lower(animationAttribute(tok, "autoreverse"));
        if (!reverse.empty() && reverse != "true" && reverse != "1" &&
            reverse != "false" && reverse != "0") return false;
        track.autoReverse = reverse == "true" || reverse == "1";
        return animationTime(tok, "begin", track.beginMs) &&
               animationTime(tok, "duration", track.durationMs) &&
               animationRepeat(tok, track.repeat) &&
               animationEndpoint(tok, "from", track.property, track.hasFrom, track.from) &&
               animationEndpoint(tok, "to", track.property, track.hasTo, track.to);
    }

    static bool registerStoryboard(ThemeElement &elem, const std::string &event) {
        if (std::find(elem.storyboardEvents.begin(), elem.storyboardEvents.end(), event) ==
            elem.storyboardEvents.end()) {
            if (elem.storyboardEvents.size() >= ThemeAnimation::MaxTracks) return false;
            elem.storyboardEvents.push_back(event);
        }
        auto &tracks = elem.animations;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
            [&](const ThemeAnimation &track) { return track.event == event; }), tracks.end());
        return true;
    }

    void parseStoryboard(XmlLexer &lexer, const XmlToken &root, ThemeElement &elem) {
        const std::string event = animationAttribute(root, "event");
        uint32_t repeat = 1;
        if (event.size() > ThemeAnimation::MaxEventLength ||
            !animationRepeat(root, repeat) || root.attrs.find("repeatat") != root.attrs.end() ||
            !registerStoryboard(elem, event)) {
            if (root.type != XmlToken::SelfClose) skipElement(lexer, root.name);
            return;
        }
        if (root.type == XmlToken::SelfClose) return;

        const size_t first = elem.animations.size();
        uint32_t duration = 0;
        XmlToken tok;
        while (lexer.next(tok)) {
            if (tok.type == XmlToken::CloseTag && tok.name == "storyboard") break;
            if (tok.type != XmlToken::OpenTag && tok.type != XmlToken::SelfClose) continue;
            if (tok.name == "animation" && conditionsMatch(tok.attrs) &&
                elem.animations.size() < ThemeAnimation::MaxTracks) {
                ThemeAnimation track;
                if (parseAnimation(tok, track)) {
                    track.event = event;
                    track.storyboardRepeat = repeat;
                    duration = std::max(duration, track.beginMs +
                        themeui::animation_detail::trackDuration(track));
                    elem.animations.push_back(std::move(track));
                }
            }
            if (tok.type != XmlToken::SelfClose) skipElement(lexer, tok.name);
        }
        for (size_t i = first; i < elem.animations.size(); ++i)
            elem.animations[i].storyboardDurationMs = duration;
    }

    static void mergeAnimations(ThemeElement &existing, const ThemeElement &elem) {
        for (const std::string &event : elem.storyboardEvents) {
            if (!registerStoryboard(existing, event)) continue;
            for (const ThemeAnimation &track : elem.animations) {
                if (track.event == event && existing.animations.size() < ThemeAnimation::MaxTracks)
                    existing.animations.push_back(track);
            }
        }
    }

    static bool usableAssetPath(const std::string &path) {
        if (fileExists(path)) return true;
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || lower(path.substr(dot)) != ".svg")
            return false;
        for (const char *extension : {".png", ".webp", ".jpg", ".jpeg"}) {
            if (fileExists(path.substr(0, dot) + extension)) return true;
        }
        return false;
    }

    std::string resolveAssetPath(const std::string &baseDir,
                                 const std::string &value) const {
        if (value.empty() || isAbsolutePath(value) ||
            value.compare(0, 2, "~/") == 0) {
            return value;
        }

        std::vector<std::string> candidates;
        candidates.push_back(joinPath(baseDir, value));
        if (!systemTheme_.empty()) {
            candidates.push_back(joinPath(
                joinPath(theme_.rootDir_, systemTheme_), value));
        }
        candidates.push_back(joinPath(theme_.rootDir_, value));

        for (const auto &candidate : candidates) {
            if (usableAssetPath(candidate)) return candidate;
        }
        return candidates.front();
    }

    ThemeView &ensureView(const std::string &name) {
        for (auto &view : theme_.views_) {
            if (view.name == name) return view;
        }
        theme_.views_.push_back({name, {}});
        return theme_.views_.back();
    }

    void mergeElement(std::vector<ThemeElement> &elements,
                      const ThemeElement &elem) {
        if (!elem.name.empty()) {
            for (auto &existing : elements) {
                // EmulationStation stores a view's elements in a map keyed by
                // element name. A later declaration may intentionally change
                // the type (for example, text -> datetime) while retaining
                // properties supplied by the earlier declaration.
                if (existing.name == elem.name) {
                    existing.type = elem.type;
                    auto nextPath = elem.props.find("path");
                    auto oldPath = existing.props.find("path");
                    if (nextPath != elem.props.end() &&
                        oldPath != existing.props.end() &&
                        nextPath->second != oldPath->second &&
                        existing.props.find("__previouspath") ==
                            existing.props.end()) {
                        // Several ArkOS art-book variants use the same
                        // element name first for the system logo and later
                        // for carousel artwork. Keep the earlier valid asset
                        // available to SystemView as a compatibility layer.
                        existing.props["__previouspath"] = oldPath->second;
                    }
                    for (const auto &kv : elem.props)
                        existing.props[kv.first] = kv.second;
                    mergeAnimations(existing, elem);
                    return;
                }
            }
        }
        elements.push_back(elem);
    }

    Theme &theme_;
    std::string systemTheme_;
    std::unordered_map<std::string, std::string> activeSubsets_;
};

void Theme::seedVariables(const std::string &rootDir,
                          const std::string &systemTheme,
                          const std::string &systemName) {
    variables_.clear();
    variables_["themepath"] = rootDir;
    variables_["themePath"] = rootDir;
    variables_["system.theme"] = systemTheme;
    variables_["system.name"] = systemName.empty() ? systemTheme : systemName;
    variables_["system.fullname"] = systemName.empty() ? systemTheme : systemName;
    variables_["system.fullName"] = systemName.empty() ? systemTheme : systemName;
    const char *widthEnv = std::getenv("LILSMOKE_SCREEN_WIDTH");
    const char *heightEnv = std::getenv("LILSMOKE_SCREEN_HEIGHT");
    int width = widthEnv ? std::atoi(widthEnv) : 640;
    int height = heightEnv ? std::atoi(heightEnv) : 480;
    if (width <= 0) width = 640;
    if (height <= 0) height = 480;
    variables_["screen.width"] = std::to_string(width);
    variables_["screen.height"] = std::to_string(height);
    char ratio[32];
    std::snprintf(ratio, sizeof(ratio), "%.6f", (double)width / (double)height);
    variables_["screen.ratio"] = ratio;
    variables_["lang"] = "";
}

void ThemeElement::getPos(float &x, float &y, float defX, float defY) const {
    auto it = props.find("pos");
    if (it == props.end()) { x = defX; y = defY; return; }
    parsePair(it->second, x, y, defX, defY);
}

void ThemeElement::getSize(float &w, float &h, float defW, float defH) const {
    auto it = props.find("size");
    if (it == props.end()) { w = defW; h = defH; return; }
    parsePair(it->second, w, h, defW, defH);
}

void ThemeElement::getOrigin(float &x, float &y, float defX, float defY) const {
    auto it = props.find("origin");
    if (it == props.end()) { x = defX; y = defY; return; }
    parsePair(it->second, x, y, defX, defY);
}

float ThemeElement::getFloat(const std::string &key, float def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    try {
        return std::stof(it->second);
    } catch (...) {
        return def;
    }
}

float ThemeElement::getFloat(const char *key, float def) const {
    for (const auto &property : props) {
        if (property.first != key) continue;
        // Numeric animation bases never need an unbounded input or exceptions.
        if (property.second.empty() || property.second.size() > 64) return def;
        const char *start = property.second.c_str();
        char *end = nullptr;
        errno = 0;
        const float value = std::strtof(start, &end);
        return end == start || errno == ERANGE || !std::isfinite(value) ? def : value;
    }
    return def;
}

bool ThemeElement::getBool(const std::string &key, bool def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    std::string v = lower(trim(it->second));
    if (v == "true" || v == "1" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "no") return false;
    return def;
}

uint32_t ThemeElement::getColor(const std::string &key, uint32_t def) const {
    auto it = props.find(key);
    if (it == props.end()) return def;
    std::string hex = trim(it->second);
    if (hex.empty()) return def;
    if (hex[0] == '#') hex = hex.substr(1);
    if (hex.size() == 3) {
        hex = std::string(2, hex[0]) + std::string(2, hex[1]) +
              std::string(2, hex[2]);
    }
    if (hex.size() != 6 && hex.size() != 8) return def;

    unsigned r = 0, g = 0, b = 0, a = 0xFF;
    if (std::sscanf(hex.c_str(), "%2x%2x%2x", &r, &g, &b) != 3)
        return def;
    if (hex.size() == 8)
        std::sscanf(hex.c_str() + 6, "%2x", &a);
    return (r << 24) | (g << 16) | (b << 8) | a;
}

const ThemeElement *ThemeView::find(const std::string &type,
                                    const std::string &name) const {
    for (auto it = elements.rbegin(); it != elements.rend(); ++it) {
        if (it->type == type && it->name == name) return &*it;
    }
    return nullptr;
}

Theme::Theme() = default;

bool Theme::load(const std::string &themeXmlPath,
                 const std::string &systemTheme,
                 const std::string &systemName) {
    ++revision_;
    loaded_ = false;
    views_.clear();
    defaultView_.clear();
    defaultTransition_.clear();
    dir_ = dirnameOf(themeXmlPath);
    rootDir_ = dir_;
    seedVariables(rootDir_, systemTheme, systemName);

    ThemeParser parser(*this, systemTheme, systemName);
    loaded_ = parser.parseFile(themeXmlPath);
    LOG_INFO("Theme: loaded %s (%zu views, %zu variables)",
             themeXmlPath.c_str(), views_.size(), variables_.size());
    return loaded_;
}

bool Theme::loadSet(const std::string &themeSetDir,
                    const std::string &systemTheme,
                    const std::string &systemName) {
    ++revision_;
    loaded_ = false;
    views_.clear();
    defaultView_.clear();
    defaultTransition_.clear();
    rootDir_ = themeSetDir;
    dir_ = themeSetDir;
    seedVariables(rootDir_, systemTheme, systemName);

    ThemeParser parser(*this, systemTheme, systemName);
    bool any = false;

    std::string systemXml;
    if (!systemTheme.empty()) {
        systemXml = joinPath(joinPath(themeSetDir, systemTheme), "theme.xml");
    }

    // An ES system theme normally includes the set-level theme.xml itself.
    // Parsing both here would apply every declaration twice and alter order.
    if (!systemXml.empty() && fileExists(systemXml)) {
        any = parser.parseFile(systemXml);
    } else {
        std::string defaultXml = joinPath(themeSetDir, "theme.xml");
        if (fileExists(defaultXml)) any = parser.parseFile(defaultXml);
    }

    loaded_ = any;
    if (loaded_) {
        LOG_INFO("Theme: loaded set %s system=%s (%zu views, %zu variables)",
                 themeSetDir.c_str(), systemTheme.c_str(),
                 views_.size(), variables_.size());
    } else if (dirExists(themeSetDir)) {
        LOG_WARN("Theme: set %s has no usable theme.xml", themeSetDir.c_str());
    }
    return loaded_;
}

const ThemeView *Theme::findView(const std::string &name) const {
    for (const auto &view : views_) {
        if (view.name == name) return &view;
    }
    return nullptr;
}

std::string Theme::variable(const std::string &name) const {
    auto it = variables_.find(name);
    if (it == variables_.end())
        it = variables_.find(lower(name));
    return it == variables_.end() ? "" : it->second;
}

}  // namespace lilsmoke
