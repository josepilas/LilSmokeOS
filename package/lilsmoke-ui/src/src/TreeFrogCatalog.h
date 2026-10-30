// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lilsmoke {

struct TreeFrogPort {
    std::string id, name, directory, boxartPath, missingData, reason;
    std::string launchType, binary, content;
    std::vector<std::string> args;
    std::vector<std::vector<std::string>> requiredFileSets;
    // Same order as requiredFileSets; an empty inner vector is a satisfied alternative.
    std::vector<std::vector<std::string>> missingFileSets;
    bool filesChecked = false;
    bool hasRunScript = false, hasBinary = false, hasContent = false;
    bool requiredFilesSatisfied = false;
    bool available() const { return filesChecked && reason.empty(); }
};

// Read-only discovery of original extracted releases, independent of the ROM index.
class TreeFrogCatalog {
public:
    static constexpr const char *DefaultRoot = "/storage/treefrog-ports";
    static constexpr size_t MaxJsonBytes = 64 * 1024;
    static constexpr size_t MaxEntries = 128;
    static constexpr size_t MaxIdBytes = 64;
    static constexpr size_t MaxNameBytes = 256;
    static constexpr size_t MaxPathBytes = 1024;
    static constexpr size_t MaxTextBytes = 2048;

    static bool validId(const std::string &id);
    static bool validRelativePath(const std::string &path);
    // Pure, bounded schema validation. Paths remain relative to the original port.
    // File availability is only populated by find/scan, never by parse.
    static std::optional<TreeFrogPort> parse(const std::string &json,
                                            const std::string &expectedId,
                                            std::string *error = nullptr);
    static std::vector<TreeFrogPort> scan(const std::string &root = DefaultRoot);
    static std::optional<TreeFrogPort> find(const std::string &id,
                                           const std::string &root = DefaultRoot,
                                           std::string *error = nullptr);
};

} // namespace lilsmoke
