// SPDX-License-Identifier: MPL-2.0
#include "TreeFrogCatalog.h"
#include "AppsProtocol.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using namespace lilsmoke;
namespace fs = std::filesystem;
using Catalog = TreeFrogCatalog;

static std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    assert(in);
    return {std::istreambuf_iterator<char>(in), {}};
}
static void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
    assert(out.good());
}
static std::string changed(const std::string &text, const char *field, json_object *value,
                           bool launch = false) {
    auto json = parseAppJson(text);
    json_object_object_add(launch ? appField(json.get(), "launch") : json.get(), field, value);
    return appSerialize(json.get());
}
static void reject(const std::string &text, const std::string &id = "sample-native") {
    std::string error;
    assert(!Catalog::parse(text, id, &error));
    assert(!error.empty());
}
static void seed(const fs::path &root, const std::string &id, const std::string &json) {
    write(root / id / "config.json", json);
    // A scan must never source, execute, rewrite, or chmod this original script.
    write(root / id / "run.sh", "#!/bin/sh\ntouch MUST_NOT_EXECUTE\n");
    write(root / id / "bin/game", "fixture binary");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(std::string(Catalog::DefaultRoot) == "/storage/treefrog-ports");
    const fs::path fixtures = argv[1];
    const auto native = read(fixtures / "native.json");
    const auto alternatives = read(fixtures / "alternatives.json");
    const auto core = read(fixtures / "libretro.json");
    auto parsed = Catalog::parse(native, "sample-native");
    assert(parsed && parsed->launchType == "native" && parsed->binary == "bin/game");
    assert(parsed->args == std::vector<std::string>({"--windowed", ""}));
    assert(!parsed->filesChecked && !parsed->available());
    parsed = Catalog::parse(core, "sample-core");
    assert(parsed && parsed->launchType == "libretro" && parsed->content == "data/game.dat");
    assert(Catalog::validId("a-Z_09") && Catalog::validId(std::string(64, 'a')));
    for (const auto &id : {"", ".", "..", "../escape", "a/b", "a\\b", "a.b", "a b", "-option", "a;cmd", "$(cmd)"})
        assert(!Catalog::validId(id));
    assert(!Catalog::validId(std::string(65, 'a')));
    assert(Catalog::validRelativePath("./bin/game") && Catalog::validRelativePath("data/a b.dat"));
    for (const auto &path : {"", ".", "..", "../outside", "/bin/game", "a/../b", "a/./b", "a//b", "a/", "C:game", "a\\b", "a\nb"})
        assert(!Catalog::validRelativePath(path));
    assert(!Catalog::validRelativePath(std::string(1025, 'p')));

    reject(read(fixtures / "invalid.json"));
    reject(read(fixtures / "traversal.json"));
    for (const auto &text : {"[]", "null", "{}", "true", "1"}) reject(text);
    reject(native + " {}");
    reject(native + std::string(1, '\0'));
    reject(changed(native, "schema_version", json_object_new_int(2)));
    reject(changed(native, "schema_version", json_object_new_double(1.0)));
    reject(changed(native, "schema_version", json_object_new_string("1")));
    reject(changed(native, "id", json_object_new_string("another-id")));
    reject(changed(native, "name", json_object_new_string("")));
    reject(changed(native, "name", json_object_new_string(std::string(257, 'n').c_str())));
    reject(changed(native, "name", json_object_new_string("a\nb")));
    reject(changed(native, "name", json_object_new_string_len("a\0b", 3)));
    reject(changed(native, "launch", json_object_new_array()));
    reject(changed(native, "type", json_object_new_string("shell"), true));
    reject(changed(native, "binary", json_object_new_string("../escape"), true));
    reject(changed(native, "binary", json_object_new_string("/tmp/game"), true));
    reject(changed(native, "content", json_object_new_string("../escape"), true));
    reject(changed(native, "args", json_object_new_string("sh -c evil"), true));
    std::cout << "schema fixtures loaded\n";

    auto args = json_object_new_array();
    json_object_array_add(args, json_object_new_int(1));
    reject(changed(native, "args", args, true));
    args = json_object_new_array();
    for (size_t i = 0; i < 129; ++i) json_object_array_add(args, json_object_new_string("a"));
    reject(changed(native, "args", args, true));
    args = json_object_new_array();
    json_object_array_add(args, json_object_new_string(std::string(1025, 'x').c_str()));
    reject(changed(native, "args", args, true));
    for (const auto &sets : {"null", "{}", "[[]]", "[\"data\"]", "[[1]]", "[[\"../escape\"]]", "[[\"ok\"],[\"../escape\"]]"})
        reject(changed(native, "required_file_sets", json_tokener_parse(sets)));
    auto sets = json_object_new_array();
    auto set = json_object_new_array();
    for (size_t i = 0; i < 129; ++i) json_object_array_add(set, json_object_new_string("data"));
    json_object_array_add(sets, set);
    reject(changed(native, "required_file_sets", sets));
    reject(changed(native, "missingdata", json_object_new_string(std::string(2049, 'm').c_str())));
    reject(changed(native, "missingdata", json_object_new_int(1)));
    assert(Catalog::parse(changed(native, "missingdata", json_object_new_string("")), "sample-native"));
    auto exact = native + std::string(Catalog::MaxJsonBytes - native.size(), ' ');
    assert(Catalog::parse(exact, "sample-native"));
    reject(exact + " ");
    std::string deep(30, '[');
    reject(deep + "0" + std::string(30, ']'));
    reject(changed(native, "name", json_object_new_string_len("\xff", 1)));

    char temp[] = "/tmp/lilsmoke-treefrog-catalog-XXXXXX";
    const char *created = mkdtemp(temp);
    assert(created);
    const fs::path sandbox = created, root = sandbox / "ports";
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{sandbox};
    assert(Catalog::scan(root).empty());
    seed(root, "sample-native", native);
    const auto folder = root / "sample-native";
    const auto originalScript = read(folder / "run.sh");
    struct stat before{}, after{};
    assert(!stat((folder / "run.sh").c_str(), &before));
    auto port = Catalog::find("sample-native", root);
    assert(port && port->available() && port->hasRunScript && port->hasBinary && port->hasContent);
    assert(port->requiredFilesSatisfied && port->missingFileSets.empty() && port->boxartPath.empty());
    write(folder / "boxart.png", "fixture art (not decoded by catalogue)");
    assert(!Catalog::find("sample-native", root)->boxartPath.empty());
    assert(Catalog::scan(root).size() == 1);

    write(folder / "config.json", alternatives);
    write(folder / "data/first.dat", "first");
    port = Catalog::find("sample-native", root);
    assert(port && !port->available() && !port->requiredFilesSatisfied);
    assert(port->missingFileSets == std::vector<std::vector<std::string>>({{"data/second.dat"}, {"alternate.dat"}}));
    assert(!port->missingData.empty());
    write(folder / "alternate.dat", "alternative");
    port = Catalog::find("sample-native", root);
    assert(port && port->available() && port->missingFileSets[1].empty());
    fs::remove(folder / "alternate.dat");
    write(folder / "data/second.dat", "second");
    assert(Catalog::find("sample-native", root)->available());
    fs::remove(folder / "data/second.dat");
    assert(!Catalog::find("sample-native", root)->available());
    fs::create_symlink(sandbox / "outside", folder / "alternate.dat");
    write(sandbox / "outside", "outside data");
    assert(!Catalog::find("sample-native", root)->available());
    fs::remove_all(folder / "data");
    fs::create_directory_symlink(sandbox, folder / "data");
    write(sandbox / "first.dat", "outside first");
    write(sandbox / "second.dat", "outside second");
    assert(!Catalog::find("sample-native", root)->available());

    seed(root, "sample-core", core);
    write(root / "sample-core/core.so", "core");
    port = Catalog::find("sample-core", root);
    assert(port && !port->available() && port->hasBinary && !port->hasContent);
    write(root / "sample-core/data/game.dat", "content");
    assert(Catalog::find("sample-core", root)->available());
    fs::create_directory_symlink(folder, root / "linked-port");
    assert(!Catalog::find("linked-port", root));
    assert(!Catalog::find("../outside", root));
    fs::create_directory_symlink(root, sandbox / "linked-root");
    assert(Catalog::scan(sandbox / "linked-root").empty());
    fs::remove(folder / "config.json");
    fs::create_symlink(root / "sample-core/config.json", folder / "config.json");
    assert(!Catalog::find("sample-native", root));
    fs::remove(folder / "config.json");
    assert(!mkfifo((folder / "config.json").c_str(), 0600));
    assert(!Catalog::find("sample-native", root));
    fs::remove(folder / "config.json");
    write(folder / "config.json", exact + " ");
    assert(!Catalog::find("sample-native", root));
    write(folder / "config.json", native);
    fs::remove(folder / "run.sh");
    assert(Catalog::find("sample-native", root)->reason == "Original run.sh is missing");
    write(folder / "run.sh", originalScript);
    fs::remove(folder / "bin/game");
    assert(Catalog::find("sample-native", root)->reason == "Launch binary is missing");
    write(folder / "bin/game", "fixture binary");
    auto hostile = changed(native, "args", json_tokener_parse("[\"$(touch MUST_NOT_EXECUTE)\",\"; exit 1\"]"), true);
    write(folder / "config.json", hostile);
    assert(Catalog::find("sample-native", root)->args[0] == "$(touch MUST_NOT_EXECUTE)");
    assert(!fs::exists("MUST_NOT_EXECUTE") && !fs::exists(folder / "MUST_NOT_EXECUTE"));
    assert(read(folder / "run.sh") == originalScript && read(folder / "config.json") == hostile);
    assert(!stat((folder / "run.sh").c_str(), &after) && before.st_mode == after.st_mode);

    const auto many = sandbox / "many";
    for (size_t i = 0; i < 140; ++i) {
        const auto id = "port-" + std::to_string(i);
        seed(many, id, changed(native, "id", json_object_new_string(id.c_str())));
    }
    assert(Catalog::scan(many).size() == Catalog::MaxEntries);
    auto list = Catalog::scan(root);
    assert(list.size() == 2 && list[0].id == "sample-core" && list[1].id == "sample-native");
    fs::remove_all(root / "sample-core");
    assert(Catalog::scan(root).size() == 1); // No catalogue or ROM cache survives entry.
    std::cout << "TreeFrog catalogue: schema, bounds, alternatives, fresh scans, containment and read-only checks passed\n";
}
