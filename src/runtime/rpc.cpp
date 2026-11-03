#include "rpc.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "config.h"
#include "mfdp/mfdp.h"
#include "json.h"

namespace mfdp {

namespace {

class Out {
 public:
  Out& raw(const std::string& s) {
    buf_ += s;
    return *this;
  }

  Out& str(const std::string& s) {
    buf_ += '"';
    size_t i = 0;
    while (i < s.size()) {
      unsigned char c = static_cast<unsigned char>(s[i]);
      if (c == '"') {
        buf_ += "\\\"";
        ++i;
      } else if (c == '\\') {
        buf_ += "\\\\";
        ++i;
      } else if (c == '\n') {
        buf_ += "\\n";
        ++i;
      } else if (c == '\r') {
        buf_ += "\\r";
        ++i;
      } else if (c == '\t') {
        buf_ += "\\t";
        ++i;
      } else if (c < 0x20) {
        char tmp[8];
        std::snprintf(tmp, sizeof(tmp), "\\u%04x", c);
        buf_ += tmp;
        ++i;
      } else if (c < 0x80) {
        buf_ += static_cast<char>(c);
        ++i;
      } else {
        size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
        bool ok = len > 0 && i + len <= s.size();
        for (size_t k = 1; ok && k < len; ++k) {
          if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) ok = false;
        }
        if (ok) {
          buf_.append(s, i, len);
          i += len;
        } else {
          buf_ += "\xEF\xBF\xBD";
          ++i;
        }
      }
    }
    buf_ += '"';
    return *this;
  }

  Out& num(uint64_t v) {
    buf_ += std::to_string(v);
    return *this;
  }

  Out& boolean(bool v) {
    buf_ += v ? "true" : "false";
    return *this;
  }

  Out& key(const char* k) {
    str(k);
    buf_ += ':';
    return *this;
  }

  const std::string& text() const { return buf_; }

 private:
  std::string buf_;
};

void warnings(Out& o, const std::vector<Warning>& ws) {
  o.raw("[");
  for (size_t i = 0; i < ws.size(); ++i) {
    if (i) o.raw(",");
    o.raw("{").key("code").str(ws[i].code).raw(",").key("message").str(ws[i].message).raw("}");
  }
  o.raw("]");
}

[[noreturn]] void bad(const std::string& why) { throw Error(ErrorCode::InvalidArgument, why); }

const json::Value* field(const json::Value& p, const char* key) {
  const json::Value* v = p.find(key);
  if (!v || v->type == json::Type::Null) return nullptr;
  return v;
}

std::string getString(const json::Value& p, const char* key, const std::string& fallback) {
  const json::Value* v = field(p, key);
  if (!v) return fallback;
  if (!v->isString()) bad(std::string("parameter '") + key + "' must be a string");
  return v->string;
}

std::string requireString(const json::Value& p, const char* key) {
  const json::Value* v = field(p, key);
  if (!v || !v->isString() || v->string.empty()) bad(std::string("parameter '") + key + "' is required");
  return v->string;
}

std::optional<bool> getBool(const json::Value& p, const char* key) {
  const json::Value* v = field(p, key);
  if (!v) return std::nullopt;
  if (!v->isBool()) bad(std::string("parameter '") + key + "' must be a boolean");
  return v->boolean;
}

std::optional<uint32_t> getUInt(const json::Value& p, const char* key) {
  const json::Value* v = field(p, key);
  if (!v) return std::nullopt;
  if (!v->isInt() || v->integer < 0 || v->integer > 0x7FFFFFFF) {
    bad(std::string("parameter '") + key + "' must be a non-negative integer");
  }
  return static_cast<uint32_t>(v->integer);
}

std::vector<std::string> getStrings(const json::Value& p, const char* key) {
  std::vector<std::string> out;
  const json::Value* v = field(p, key);
  if (!v) return out;
  if (v->isString()) return splitList(v->string);
  if (!v->isArray()) bad(std::string("parameter '") + key + "' must be an array of strings");
  for (const auto& item : v->array) {
    if (!item.isString()) bad(std::string("parameter '") + key + "' must hold only strings");
    out.push_back(item.string);
  }
  return out;
}

WriteOptions writeOptions(const json::Value& p) {
  WriteOptions o;
  o.src = getStrings(p, "src");
  if (const json::Value* mem = field(p, "memory")) {
    if (!mem->isArray()) bad("parameter 'memory' must be an array");
    for (const auto& m : mem->array) {
      if (!m.isObject()) bad("each memory source must be an object");
      MemorySource ms;
      ms.filename = requireString(m, "filename");
      ms.content = getString(m, "content", "");
      o.memory.push_back(std::move(ms));
    }
  }
  o.kwrds = getString(p, "kwrds", "*");
  o.inc = getBool(p, "inc");
  o.include = getBool(p, "include");
  o.inctyps = getBool(p, "inctyps");
  o.include_types = getBool(p, "include_types");
  if (auto v = getString(p, "import_mode", ""); !v.empty()) {
    ImportMode m;
    if (!parseImportMode(v, m)) bad("invalid import_mode");
    o.import_mode = m;
  }
  if (auto v = getBool(p, "legacy_inctyps")) o.legacy_inctyps = *v;
  if (auto v = getUInt(p, "budget")) o.budget = *v;
  o.lang = getString(p, "lang", "auto");
  if (auto v = getBool(p, "hash")) o.hash = *v;
  if (auto v = getString(p, "mode", ""); !v.empty()) {
    if (!parseMode(v, o.mode)) bad("invalid mode");
  }
  if (auto v = getBool(p, "deps")) o.deps = *v;
  if (auto v = getUInt(p, "dep_depth")) o.dep_depth = *v;
  if (auto v = getBool(p, "lines")) o.lines = *v;
  if (auto v = getBool(p, "combined")) o.combined = *v;
  o.output = getString(p, "output", "");
  o.output_dir = getString(p, "output_dir", "");
  if (auto v = getBool(p, "overwrite")) o.overwrite = *v;
  return o;
}

std::string doWrite(const json::Value& p) {
  WriteResult r = write(writeOptions(p));
  Out o;
  o.raw("{").key("outputs").raw("[");
  for (size_t i = 0; i < r.outputs.size(); ++i) {
    if (i) o.raw(",");
    o.str(r.outputs[i]);
  }
  o.raw("],").key("summaries").raw("[");
  for (size_t i = 0; i < r.summaries.size(); ++i) {
    const auto& s = r.summaries[i];
    if (i) o.raw(",");
    o.raw("{").key("output").str(s.output).raw(",").key("storedBytes").num(s.storedBytes).raw(",");
    o.key("rawBytes").num(s.rawBytes).raw(",").key("files").num(s.files).raw(",");
    o.key("symbolsStored").num(s.symbolsStored).raw(",").key("symbolsParsed").num(s.symbolsParsed).raw("}");
  }
  o.raw("],").key("warnings");
  warnings(o, r.warnings);
  o.raw("}");
  return o.text();
}

std::string doRead(const json::Value& p) {
  Artifact a = read(requireString(p, "artifact"));
  uint64_t symbols = 0;
  for (const auto& f : a.files) symbols += f.symbols.size();
  Out o;
  o.raw("{").key("valid").boolean(true).raw(",").key("versionMajor").num(a.meta.versionMajor).raw(",");
  o.key("versionMinor").num(a.meta.versionMinor).raw(",").key("files").num(a.files.size()).raw(",");
  o.key("symbols").num(symbols).raw("}");
  return o.text();
}

std::string doInspect(const json::Value& p) {
  InspectResult r = inspect(requireString(p, "artifact"));
  Out o;
  o.raw("{").key("versionMajor").num(r.versionMajor).raw(",").key("versionMinor").num(r.versionMinor).raw(",");
  o.key("flags").num(r.flags).raw(",").key("compression").str(r.compression == 1 ? "zlib" : "none").raw(",");
  o.key("storedBytes").num(r.storedBytes).raw(",").key("rawBytes").num(r.rawBytes).raw(",");
  o.key("requestedMode").str(modeName(r.requestedMode)).raw(",").key("resolvedMode").str(modeName(r.resolvedMode)).raw(",");
  o.key("importMode").str(importModeName(r.importMode)).raw(",").key("combined").boolean(r.combined).raw(",");
  o.key("budget").num(r.budget).raw(",").key("depDepth").num(r.depDepth).raw(",").key("kwrds").str(r.kwrds).raw(",");
  o.key("files").raw("[");
  for (size_t i = 0; i < r.files.size(); ++i) {
    const auto& f = r.files[i];
    if (i) o.raw(",");
    o.raw("{").key("path").str(f.path).raw(",").key("filename").str(f.filename).raw(",");
    o.key("language").str(f.language).raw(",").key("sourceSize").num(f.sourceSize).raw(",");
    o.key("hasHash").boolean(f.hasHash).raw(",").key("sha256").str(f.hashHex).raw(",");
    o.key("totalSymbols").num(f.totalSymbols).raw(",").key("storedSymbols").num(f.storedSymbols).raw(",");
    o.key("selectedSymbols").num(f.selectedSymbols).raw(",").key("totalImports").num(f.totalImports).raw(",");
    o.key("storedImports").num(f.storedImports).raw(",").key("dependencies").num(f.dependencies).raw(",");
    o.key("fragments").num(f.fragments).raw("}");
  }
  o.raw("],").key("warnings");
  warnings(o, r.warnings);
  o.raw("}");
  return o.text();
}

std::string doExtract(const json::Value& p) {
  ExtractOptions e;
  e.kwrds = getString(p, "kwrds", "*");
  if (auto v = getUInt(p, "budget")) e.budget = *v;
  std::string expand = getString(p, "expand", "auto");
  if (expand == "auto") e.expand = Expand::Auto;
  else if (expand == "always") e.expand = Expand::Always;
  else if (expand == "never") e.expand = Expand::Never;
  else bad("invalid expand");
  if (auto v = getBool(p, "deps")) e.deps = *v;
  if (auto v = getBool(p, "imports")) e.imports = *v;
  ExtractResult r = extract(requireString(p, "artifact"), e);
  Out o;
  o.raw("{").key("text").str(r.text).raw(",").key("tokens").num(r.tokens).raw(",");
  o.key("tokensAreApproximate").boolean(true).raw(",").key("budget").num(r.budget).raw(",");
  o.key("symbolsStored").num(r.symbolsStored).raw(",").key("symbolsPresented").num(r.symbolsPresented).raw(",");
  o.key("fragmentsAvailable").num(r.fragmentsAvailable).raw(",").key("fragmentsIncluded").num(r.fragmentsIncluded).raw(",");
  o.key("fragmentsOmitted").num(r.fragmentsOmitted).raw(",").key("omittedTokens").num(r.omittedTokens).raw(",");
  o.key("warnings");
  warnings(o, r.warnings);
  o.raw("}");
  return o.text();
}

std::string doVerify(const json::Value& p) {
  Artifact a = read(requireString(p, "artifact"));
  std::vector<OriginalSource> originals;
  if (const json::Value* orig = field(p, "originals")) {
    if (!orig->isArray()) bad("parameter 'originals' must be an array");
    for (const auto& item : orig->array) {
      if (!item.isObject()) bad("each original must be an object");
      OriginalSource os;
      os.path = requireString(item, "path");
      os.content = getString(item, "content", "");
      originals.push_back(std::move(os));
    }
  }
  std::vector<std::string> paths = getStrings(p, "src");
  VerifyReport report;
  if (!paths.empty()) {
    for (const auto& path : paths) {
      std::ifstream in(path, std::ios::binary);
      if (!in) throw Error(ErrorCode::Io, "cannot open original source", path);
      std::ostringstream buf;
      buf << in.rdbuf();
      OriginalSource os;
      os.path = path;
      os.content = buf.str();
      originals.push_back(std::move(os));
    }
  }
  report = verify(a, originals);
  Out o;
  o.raw("{").key("ok").boolean(report.ok).raw(",").key("entries").raw("[");
  for (size_t i = 0; i < report.entries.size(); ++i) {
    const auto& e = report.entries[i];
    const char* status = "match";
    if (e.status == VerifyStatus::Stale) status = "stale";
    else if (e.status == VerifyStatus::NoHashStored) status = "no-hash";
    else if (e.status == VerifyStatus::MissingOriginal) status = "missing";
    if (i) o.raw(",");
    o.raw("{").key("recordPath").str(e.recordPath).raw(",").key("originalPath").str(e.originalPath).raw(",");
    o.key("status").str(status).raw(",").key("expected").str(e.expectedHex).raw(",").key("actual").str(e.actualHex).raw("}");
  }
  o.raw("],").key("unmatchedOriginals").raw("[");
  for (size_t i = 0; i < report.unmatchedOriginals.size(); ++i) {
    if (i) o.raw(",");
    o.str(report.unmatchedOriginals[i]);
  }
  o.raw("]}");
  return o.text();
}

std::string doInfo() {
  AppInfo info = loadAppInfo();
  Out o;
  o.raw("{").key("displayName").str(info.displayName).raw(",").key("slug").str(info.slug).raw(",");
  o.key("name").str(info.name).raw(",").key("author").str(info.author).raw(",");
  o.key("version").str(info.version).raw(",").key("description").str(info.description).raw(",");
  o.key("license").str(info.license).raw(",").key("formatVersion").str("1.0").raw("}");
  return o.text();
}

}

std::string handleRpc(const std::string& requestJson) {
  try {
    json::Value req;
    try {
      req = json::parse(requestJson);
    } catch (const Error& e) {
      throw Error(ErrorCode::InvalidArgument, std::string("invalid JSON request: ") + e.what());
    }
    if (!req.isObject()) bad("request must be a JSON object");
    std::string method = requireString(req, "method");
    json::Value empty;
    empty.type = json::Type::Object;
    const json::Value* params = field(req, "params");
    if (params && !params->isObject()) bad("'params' must be an object");
    const json::Value& p = params ? *params : empty;
    std::string result;
    if (method == "write") result = doWrite(p);
    else if (method == "read") result = doRead(p);
    else if (method == "inspect") result = doInspect(p);
    else if (method == "extract") result = doExtract(p);
    else if (method == "verify") result = doVerify(p);
    else if (method == "info") result = doInfo();
    else bad("unknown method '" + method + "'");
    return "{\"ok\":true,\"result\":" + result + "}";
  } catch (const Error& e) {
    Out o;
    o.raw("{").key("ok").boolean(false).raw(",").key("error").raw("{").key("code").str(errorCodeName(e.code()));
    o.raw(",").key("message").str(e.what()).raw(",").key("file").str(e.file()).raw(",");
    o.key("line").num(e.line()).raw(",").key("column").num(e.column()).raw("}}");
    return o.text();
  } catch (const std::exception& e) {
    Out o;
    o.raw("{").key("ok").boolean(false).raw(",").key("error").raw("{").key("code").str("Internal");
    o.raw(",").key("message").str(e.what()).raw(",").key("file").str("").raw(",").key("line").num(0);
    o.raw(",").key("column").num(0).raw("}}");
    return o.text();
  }
}

}
