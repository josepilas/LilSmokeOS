// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Jose Pilas.
// Repository qualification only: reads the actual registry, does not copy it.
#include "EmulatorSystems.h"
#include <json-c/json.h>
#include <iostream>

int main() {
    auto *root = json_object_new_object();
    for (const auto &system : lilsmoke::emulatorSystemDefinitions()) {
        auto *aliases = json_object_new_array();
        for (const auto &alias : system.aliases)
            json_object_array_add(aliases, json_object_new_string(alias.c_str()));
        json_object_object_add(root, system.id.c_str(), aliases);
    }
    std::cout << json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN) << '\n';
    json_object_put(root);
}
