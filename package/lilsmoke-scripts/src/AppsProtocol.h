#pragma once
#include "EmulatorSystems.h"
#include <json-c/json.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace lilsmoke {
using AppJson = std::shared_ptr<json_object>;
inline AppJson appJson(json_object *p) {
  return AppJson(p, [](json_object *j) {
    if (j)
      json_object_put(j);
  });
}
inline AppJson parseAppJson(const std::string &text) {
  if (text.size() > 2 * 1024 * 1024)
    throw std::runtime_error("JSON exceeds metadata budget");
  json_tokener *t = json_tokener_new_ex(24);
  if (!t)
    throw std::runtime_error("Cannot allocate JSON parser");
  json_tokener_set_flags(t, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
  auto j = appJson(json_tokener_parse_ex(t, text.c_str(),
                                         static_cast<int>(text.size() + 1)));
  auto e = json_tokener_get_error(t);
  json_tokener_free(t);
  if (e != json_tokener_success || !j)
    throw std::runtime_error("Invalid JSON metadata");
  return j;
}
inline json_object *appField(json_object *j, const char *name) {
  json_object *v = nullptr;
  if (j)
    json_object_object_get_ex(j, name, &v);
  return v;
}
inline std::string appString(json_object *j, const char *name,
                             const std::string &fallback = {}) {
  auto v = appField(j, name);
  if (!v)
    return fallback;
  if (!json_object_is_type(v, json_type_string))
    throw std::runtime_error(std::string("Invalid field: ") + name);
  return std::string(json_object_get_string(v), json_object_get_string_len(v));
}
inline void appSet(json_object *j, const char *key, const std::string &s) {
  json_object_object_add(
      j, key, json_object_new_string_len(s.data(), static_cast<int>(s.size())));
}
inline void appNumber(json_object *j, const char *key, uint64_t n) {
  json_object_object_add(j, key, json_object_new_uint64(n));
}
inline std::vector<std::string> appOpenCores(json_object *j) {
  json_object *value = nullptr;
  std::vector<std::string> result;
  if (!j || !json_object_object_get_ex(j, "open_core", &value)) return result;
  if (!value) throw std::runtime_error("Invalid open_core field");
  const auto add = [&](json_object *entry) {
    if (!entry || !json_object_is_type(entry, json_type_string))
      throw std::runtime_error("open_core needs a system ID or an array of system IDs");
    std::string id(json_object_get_string(entry), json_object_get_string_len(entry));
    const auto *system = emulatorSystemDefinition(id);
    if (!system) throw std::runtime_error("Unknown open_core system: " + id);
    if (std::find(result.begin(), result.end(), system->id) == result.end()) result.push_back(system->id);
  };
  if (json_object_is_type(value, json_type_string)) add(value);
  else if (json_object_is_type(value, json_type_array)) {
    const auto count = json_object_array_length(value);
    if (!count || count > emulatorSystems().size()) throw std::runtime_error("Invalid open_core list size");
    for (size_t i = 0; i < count; ++i) add(json_object_array_get_idx(value, i));
  } else throw std::runtime_error("Invalid open_core field");
  return result;
}
inline std::string appSerialize(json_object *j) {
  return json_object_to_json_string_ext(j, JSON_C_TO_STRING_PLAIN);
}
} // namespace lilsmoke
