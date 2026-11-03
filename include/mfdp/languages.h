#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mfdp {

struct LanguageEntry {
  std::string id;
  std::vector<std::string> extensions;
  bool implemented;
  std::string backend;
};

const std::vector<LanguageEntry>& languageMap();
const LanguageEntry* findLanguageById(const std::string& id);
const LanguageEntry* findLanguageByExtension(const std::string& extension);
std::optional<std::string> detectLanguage(const std::string& path);
std::string resolveLanguage(const std::string& path, const std::string& override);

}
