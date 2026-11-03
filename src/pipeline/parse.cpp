#include <algorithm>

#include "json.h"
#include "parsed.h"
#include "sha256.h"

namespace mfdp {

namespace {

[[noreturn]] void bad(const std::string& file, const std::string& why) {
  throw Error(ErrorCode::AdapterFailed, "parser adapter returned malformed data: " + why, file);
}

std::string getString(const json::Value& obj, const char* key, const std::string& file,
                      bool required = false) {
  const json::Value* v = obj.find(key);
  if (!v || v->type == json::Type::Null) {
    if (required) bad(file, std::string("missing field '") + key + "'");
    return std::string();
  }
  if (!v->isString()) bad(file, std::string("field '") + key + "' is not a string");
  return v->string;
}

uint32_t getUInt(const json::Value& obj, const char* key, const std::string& file) {
  const json::Value* v = obj.find(key);
  if (!v || v->type == json::Type::Null) return 0;
  if (!v->isInt() || v->integer < 0 || v->integer > 0x7FFFFFFF) {
    bad(file, std::string("field '") + key + "' is not a valid integer");
  }
  return static_cast<uint32_t>(v->integer);
}

bool getBool(const json::Value& obj, const char* key, const std::string& file) {
  const json::Value* v = obj.find(key);
  if (!v || v->type == json::Type::Null) return false;
  if (!v->isBool()) bad(file, std::string("field '") + key + "' is not a boolean");
  return v->boolean;
}

std::vector<std::string> getStrings(const json::Value& obj, const char* key, const std::string& file) {
  std::vector<std::string> out;
  const json::Value* v = obj.find(key);
  if (!v || v->type == json::Type::Null) return out;
  if (!v->isArray()) bad(file, std::string("field '") + key + "' is not an array");
  for (const auto& item : v->array) {
    if (!item.isString()) bad(file, std::string("array '") + key + "' holds a non-string");
    out.push_back(item.string);
  }
  return out;
}

const std::vector<json::Value>& getArray(const json::Value& obj, const char* key,
                                         const std::string& file) {
  static const std::vector<json::Value> empty;
  const json::Value* v = obj.find(key);
  if (!v || v->type == json::Type::Null) return empty;
  if (!v->isArray()) bad(file, std::string("field '") + key + "' is not an array");
  return v->array;
}

}

ParsedFile parseSource(const std::string& path, const std::string& filename,
                       const std::string& content, const std::string& language) {
  ParsedFile pf;
  pf.path = path;
  pf.filename = filename;
  pf.language = language;
  pf.content = content;
  pf.hash = sha256(content);

  std::string raw = runAdapterProcess(language, filename, content);
  json::Value root = json::parse(raw);
  if (!root.isObject()) bad(path, "top-level value is not an object");

  const json::Value* ok = root.find("ok");
  if (!ok || !ok->isBool()) bad(path, "missing 'ok' field");
  if (!ok->boolean) {
    const json::Value* err = root.find("error");
    std::string message = "parse failed";
    std::string kind;
    uint32_t line = 0;
    uint32_t col = 0;
    if (err && err->isObject()) {
      message = getString(*err, "message", path);
      kind = getString(*err, "kind", path);
      line = getUInt(*err, "line", path);
      col = getUInt(*err, "column", path);
    }
    if (kind == "adapter_unavailable") throw Error(ErrorCode::AdapterUnavailable, message, path);
    throw Error(ErrorCode::ParseFailed, message, path, line, col);
  }

  for (const auto& iv : getArray(root, "imports", path)) {
    if (!iv.isObject()) bad(path, "import entry is not an object");
    Import imp;
    imp.line = getUInt(iv, "line", path);
    imp.endLine = getUInt(iv, "endLine", path);
    imp.text = getString(iv, "text", path);
    imp.module = getString(iv, "module", path);
    imp.star = getBool(iv, "star", path);
    for (const auto& bv : getArray(iv, "bindings", path)) {
      if (!bv.isObject()) bad(path, "binding entry is not an object");
      Binding b;
      b.local = getString(bv, "local", path, true);
      b.imported = getString(bv, "imported", path, true);
      b.module = getString(bv, "module", path);
      uint32_t depth = getUInt(bv, "depth", path);
      b.depth = depth == 0 ? 1 : depth;
      imp.bindings.push_back(std::move(b));
    }
    pf.imports.push_back(std::move(imp));
  }

  const json::Value* exportsValue = root.find("exports");
  if (exportsValue && exportsValue->isArray()) pf.exports = getStrings(root, "exports", path);

  const auto& symbolValues = getArray(root, "symbols", path);
  for (size_t idx = 0; idx < symbolValues.size(); ++idx) {
    const json::Value& sv = symbolValues[idx];
    if (!sv.isObject()) bad(path, "symbol entry is not an object");
    RawSymbol rs;
    Symbol& s = rs.sym;
    std::string kind = getString(sv, "kind", path, true);
    if (!parseKind(kind, s.kind)) bad(path, "unknown symbol kind '" + kind + "'");
    const json::Value* parent = sv.find("parent");
    int64_t parentIndex = -1;
    if (parent && parent->isInt()) parentIndex = parent->integer;
    if (parentIndex >= static_cast<int64_t>(idx)) bad(path, "symbol parent does not precede child");
    if (parentIndex < -1) bad(path, "invalid symbol parent");
    s.parentPlus1 = static_cast<uint32_t>(parentIndex + 1);
    s.name = getString(sv, "name", path, true);
    s.qualified = getString(sv, "qualified", path);
    if (s.qualified.empty()) s.qualified = s.name;
    s.signature = getString(sv, "signature", path);
    s.returnType = getString(sv, "returns", path);
    s.doc = getString(sv, "doc", path);
    s.fragment = getString(sv, "source", path);
    s.decorators = getStrings(sv, "decorators", path);
    s.bases = getStrings(sv, "bases", path);
    s.startLine = getUInt(sv, "startLine", path);
    s.endLine = getUInt(sv, "endLine", path);
    std::string exported = getString(sv, "exported", path);
    if (exported == "yes") s.exported = Tri::Yes;
    else if (exported == "no") s.exported = Tri::No;
    else s.exported = Tri::Unknown;
    for (const auto& pv : getArray(sv, "params", path)) {
      if (!pv.isObject()) bad(path, "param entry is not an object");
      Param p;
      p.name = getString(pv, "name", path);
      p.annotation = getString(pv, "annotation", path);
      p.defaultValue = getString(pv, "default", path);
      s.params.push_back(std::move(p));
    }
    rs.calls = getStrings(sv, "calls", path);
    rs.refs = getStrings(sv, "refs", path);
    rs.defaultExport = getBool(sv, "defaultExport", path);
    pf.symbols.push_back(std::move(rs));
  }
  return pf;
}

}
