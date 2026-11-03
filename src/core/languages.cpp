#include "mfdp/languages.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "mfdp/errors.h"

namespace mfdp {

namespace {

std::string lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::vector<LanguageEntry> buildMap() {
  return {
      {"python", {".py", ".pyw"}, true, "python-ast"},
      {"typescript", {".ts", ".tsx", ".mts", ".cts"}, true, "typescript-compiler"},
      {"javascript", {".js", ".jsx", ".mjs", ".cjs"}, true, "typescript-compiler"},
      {"c", {".c", ".h"}, true, "libclang"},
      {"cpp", {".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx"}, true, "libclang"},
      {"go", {".go"}, true, "go-parser"},
      {"ruby", {".rb"}, false, ""},
      {"rust", {".rs"}, true, "syn"},
      {"java", {".java"}, true, "javac-tree-api"},
      {"csharp", {".cs"}, true, "roslyn"},
      {"php", {".php"}, false, ""},
      {"swift", {".swift"}, false, ""},
      {"kotlin", {".kt", ".kts"}, false, ""},
      {"html", {".html", ".htm"}, false, ""},
      {"css", {".css", ".scss"}, false, ""},
      {"sql", {".sql"}, false, ""},
      {"bash", {".sh", ".bash"}, false, ""},
      {"powershell", {".ps1"}, false, ""},
      {"json", {".json"}, false, ""},
      {"yaml", {".yaml", ".yml"}, false, ""},
      {"toml", {".toml"}, false, ""},
      {"xml", {".xml"}, false, ""},
      {"vue", {".vue"}, false, ""},
      {"svelte", {".svelte"}, false, ""},
  };
}

}

const std::vector<LanguageEntry>& languageMap() {
  static const std::vector<LanguageEntry> map = buildMap();
  return map;
}

const LanguageEntry* findLanguageById(const std::string& id) {
  std::string wanted = lower(id);
  if (wanted == "ts") wanted = "typescript";
  if (wanted == "js") wanted = "javascript";
  if (wanted == "py") wanted = "python";
  if (wanted == "c++") wanted = "cpp";
  for (const auto& entry : languageMap()) {
    if (entry.id == wanted) return &entry;
  }
  return nullptr;
}

const LanguageEntry* findLanguageByExtension(const std::string& extension) {
  std::string wanted = lower(extension);
  for (const auto& entry : languageMap()) {
    for (const auto& ext : entry.extensions) {
      if (ext == wanted) return &entry;
    }
  }
  return nullptr;
}

std::optional<std::string> detectLanguage(const std::string& path) {
  std::string ext = std::filesystem::path(path).extension().string();
  if (ext.empty()) return std::nullopt;
  const LanguageEntry* entry = findLanguageByExtension(ext);
  if (!entry) return std::nullopt;
  return entry->id;
}

std::string resolveLanguage(const std::string& path, const std::string& override) {
  const LanguageEntry* entry = nullptr;
  if (override.empty() || lower(override) == "auto") {
    std::string ext = std::filesystem::path(path).extension().string();
    entry = ext.empty() ? nullptr : findLanguageByExtension(ext);
    if (!entry) {
      throw Error(ErrorCode::UnsupportedLanguage,
                  "cannot detect language from extension '" + ext + "'; pass lang explicitly", path);
    }
  } else {
    entry = findLanguageById(override);
    if (!entry) {
      throw Error(ErrorCode::UnsupportedLanguage, "unknown language '" + override + "'", path);
    }
  }
  if (!entry->implemented) {
    throw Error(ErrorCode::UnsupportedLanguage,
                "language '" + entry->id + "' is mapped but parsing is not implemented", path);
  }
  return entry->id;
}

}
