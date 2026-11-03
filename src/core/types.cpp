#include "mfdp/types.h"

#include <algorithm>
#include <cctype>

namespace mfdp {

namespace {
std::string lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}
}

const char* kindName(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::Function: return "function";
    case SymbolKind::Method: return "method";
    case SymbolKind::Constructor: return "constructor";
    case SymbolKind::Class: return "class";
    case SymbolKind::Interface: return "interface";
    case SymbolKind::TypeAlias: return "type";
    case SymbolKind::Enum: return "enum";
    case SymbolKind::Variable: return "variable";
    case SymbolKind::Constant: return "constant";
    case SymbolKind::Namespace: return "namespace";
    case SymbolKind::Property: return "property";
  }
  return "unknown";
}

bool parseKind(const std::string& name, SymbolKind& out) {
  for (uint8_t i = 0; i <= kSymbolKindMax; ++i) {
    if (name == kindName(static_cast<SymbolKind>(i))) {
      out = static_cast<SymbolKind>(i);
      return true;
    }
  }
  return false;
}

bool isTypeKind(SymbolKind kind) {
  return kind == SymbolKind::Class || kind == SymbolKind::Interface ||
         kind == SymbolKind::TypeAlias || kind == SymbolKind::Enum;
}

bool isContainerKind(SymbolKind kind) {
  return kind == SymbolKind::Class || kind == SymbolKind::Interface || kind == SymbolKind::Namespace;
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Auto: return "auto";
    case Mode::Symbols: return "symbols";
    case Mode::Full: return "full";
  }
  return "unknown";
}

bool parseMode(const std::string& name, Mode& out) {
  std::string n = lower(name);
  if (n == "auto") out = Mode::Auto;
  else if (n == "symbols") out = Mode::Symbols;
  else if (n == "full") out = Mode::Full;
  else return false;
  return true;
}

const char* importModeName(ImportMode mode) {
  switch (mode) {
    case ImportMode::Relevant: return "relevant";
    case ImportMode::All: return "all";
    case ImportMode::None: return "none";
  }
  return "unknown";
}

bool parseImportMode(const std::string& name, ImportMode& out) {
  std::string n = lower(name);
  if (n == "relevant") out = ImportMode::Relevant;
  else if (n == "all") out = ImportMode::All;
  else if (n == "none") out = ImportMode::None;
  else return false;
  return true;
}

const char* depKindName(DepKind kind) {
  switch (kind) {
    case DepKind::Call: return "call";
    case DepKind::Ref: return "ref";
    case DepKind::Base: return "base";
  }
  return "unknown";
}

const char* depStatusName(DepStatus status) {
  switch (status) {
    case DepStatus::SameFile: return "same-file";
    case DepStatus::CrossFile: return "cross-file";
    case DepStatus::External: return "external";
    case DepStatus::Unresolved: return "unresolved";
  }
  return "unknown";
}

const char* triName(Tri value) {
  switch (value) {
    case Tri::No: return "no";
    case Tri::Yes: return "yes";
    case Tri::Unknown: return "unknown";
  }
  return "unknown";
}

bool parseBool(const std::string& text, bool& out) {
  std::string n = lower(text);
  if (n == "true" || n == "1" || n == "yes" || n == "on") out = true;
  else if (n == "false" || n == "0" || n == "no" || n == "off") out = false;
  else return false;
  return true;
}

}
