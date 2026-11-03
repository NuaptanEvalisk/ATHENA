/******************************************************************************
* MODULE     : conversion_options.cpp
* DESCRIPTION: Declarative defaults and detached per-request option values
* COPYRIGHT  : (C) 2026 ATHENA authors
* GNU GPL version 3 or later; see LICENSE.
******************************************************************************/
#include "conversion_options.hpp"
#include "file.hpp"
#include "System/Boot/boot.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <set>

namespace {
using json= nlohmann::json;
std::string bytes (string value) { return {value.data (), std::size_t (N(value))}; }
string text (const std::string& value) { return string (value.data (), value.size ()); }

struct OptionsRegistry {
  std::set<std::string> keys;
  std::map<std::pair<std::string,std::string>, std::set<std::string>> routes;
  OptionsRegistry () {
    string source;
    if (load_string (url ("$ATHENA_PATH/misc/convert/options.json"), source, false))
      throw std::runtime_error ("Cannot load conversion option registry");
    const auto data= json::parse (source.data (), source.data () + N(source));
    if (data.at ("version").get<int> () != 1)
      throw std::runtime_error ("Unsupported conversion option registry version");
    std::map<std::string, std::set<std::string>> groups;
    for (const auto& group: data.at ("groups").items ()) {
      auto values= group.value ().get<std::set<std::string>> ();
      keys.insert (values.begin (), values.end ());
      groups.emplace (group.key (), std::move (values));
    }
    for (const auto& route: data.at ("routes")) {
      const auto& values= groups.at (route.at ("group").get<std::string> ());
      for (const auto& from: route.at ("from"))
        for (const auto& to: route.at ("to"))
          if (!routes.emplace (std::make_pair (from.get<std::string> (), to.get<std::string> ()), values).second)
            throw std::runtime_error ("Duplicate conversion option route");
    }
  }
};
const OptionsRegistry& registry () { static const OptionsRegistry data; return data; }

conversion_options current_values (const std::set<std::string>& keys) {
  conversion_options result;
  for (const auto& key: keys)
    result.emplace (key, bytes (get_user_preference (text (key), "")));
  return result;
}
}

conversion_options conversion_options_snapshot () {
  return current_values (registry ().keys);
}

string encode_conversion_export_request (const conversion_export_request& request) {
  return text (json {{"format", bytes (request.format)}, {"options", request.options}}.dump ());
}

conversion_export_request decode_conversion_export_request (string source) {
  const auto data= json::parse (source.data (), source.data () + N(source));
  return {text (data.at ("format").get<std::string> ()),
          data.at ("options").get<conversion_options> ()};
}
