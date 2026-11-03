#include "config.h"

#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "mfdp/mfdp.h"
#include "json.h"

#ifndef MFDP_DEFAULT_ROOT
#define MFDP_DEFAULT_ROOT ""
#endif

namespace mfdp {

namespace {

namespace fs = std::filesystem;

bool isDir(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

bool looksLikeRoot(const fs::path& dir) { return isDir((dir / "config" / "adapters").string()); }

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string lowerCase(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string computeRoot() {
  std::vector<fs::path> starts;
  const char* env = std::getenv("MFDP_ROOT");
  if (env && *env) {
    if (!looksLikeRoot(env)) {
      throw Error(ErrorCode::AdapterUnavailable,
                  std::string("MFDP_ROOT does not contain config/adapters: ") + env);
    }
    return fs::path(env).lexically_normal().string();
  }
  if (std::string(MFDP_DEFAULT_ROOT).size() > 0) starts.push_back(MFDP_DEFAULT_ROOT);
  std::error_code ec;
  starts.push_back(fs::current_path(ec));
  char exe[4096];
  ssize_t n = -1;
#if defined(__linux__)
  n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
#elif defined(__APPLE__)
  uint32_t size = sizeof(exe) - 1;
  if (_NSGetExecutablePath(exe, &size) == 0) n = static_cast<ssize_t>(std::strlen(exe));
#endif
  if (n > 0) {
    exe[n] = 0;
    fs::path dir = fs::path(exe).parent_path();
    starts.push_back(dir);
    starts.push_back(dir / ".." / "share" / "mfdp");
  }
  for (const fs::path& start : starts) {
    fs::path cur = start.lexically_normal();
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
      if (looksLikeRoot(cur)) return cur.string();
      fs::path parent = cur.parent_path();
      if (parent == cur) break;
      cur = parent;
    }
  }
  throw Error(ErrorCode::AdapterUnavailable,
              "could not locate the MFDP root (a directory holding config/adapters); set MFDP_ROOT");
}

}

std::string findRoot() {
  static const std::string root = computeRoot();
  return root;
}

std::map<std::string, std::string> parseEnvText(const std::string& text) {
  std::map<std::string, std::string> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.compare(0, 7, "export ") == 0) t = trim(t.substr(7));
    size_t eq = t.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(t.substr(0, eq));
    std::string value = trim(t.substr(eq + 1));
    if (key.empty()) continue;
    if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
      char quote = value[0];
      size_t close = value.find(quote, 1);
      value = close == std::string::npos ? value.substr(1) : value.substr(1, close - 1);
    } else {
      for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '#' && i > 0 && (value[i - 1] == ' ' || value[i - 1] == '\t')) {
          value = trim(value.substr(0, i));
          break;
        }
      }
      while (!value.empty() && value.back() == '\\') value = trim(value.substr(0, value.size() - 1));
    }
    out[key] = value;
  }
  return out;
}

std::string envValue(const std::string& key, const std::string& fallback) {
  const char* v = std::getenv(key.c_str());
  if (v && *v) return v;
  std::string envPath = (fs::path(findRoot()) / ".env").string();
  if (isFile(envPath)) {
    auto values = parseEnvText(slurp(envPath));
    auto it = values.find(key);
    if (it != values.end() && !it->second.empty()) return it->second;
  }
  return fallback;
}

AppInfo loadAppInfo() {
  AppInfo info;
  std::string rel = envValue("APP_INFO", "./config/config.json");
  fs::path path = (fs::path(findRoot()) / rel).lexically_normal();
  if (!isFile(path.string())) return info;
  try {
    json::Value v = json::parse(slurp(path.string()));
    auto str = [&](const char* k, std::string& out) {
      const json::Value* f = v.find(k);
      if (f && f->isString()) out = f->string;
    };
    str("displayName", info.displayName);
    str("slug", info.slug);
    str("name", info.name);
    str("author", info.author);
    str("version", info.version);
    str("description", info.description);
    str("license", info.license);
  } catch (const Error&) {
  }
  return info;
}

AdapterSpec resolveAdapter(const std::string& language, const std::string& filename) {
  std::string name = envValue("PROGRAMMING_ADAPTERS", "default");
  for (char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
      throw Error(ErrorCode::InvalidArgument,
                  "PROGRAMMING_ADAPTERS must be a bare configuration filename without extension");
    }
  }
  if (name.empty()) name = "default";
  fs::path configPath = fs::path(findRoot()) / "config" / "adapters" / (name + ".json");
  if (!isFile(configPath.string())) {
    throw Error(ErrorCode::AdapterUnavailable, "adapter configuration not found: " + configPath.string());
  }
  json::Value root = json::parse(slurp(configPath.string()));
  const json::Value* table = root.find("adapters");
  if (!table || !table->isObject()) {
    throw Error(ErrorCode::AdapterUnavailable, "adapter configuration has no 'adapters' object: " + configPath.string());
  }
  std::string ext = lowerCase(fs::path(filename).extension().string());
  if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
  const json::Value* entry = nullptr;
  for (const std::string& key : {ext, language}) {
    if (key.empty()) continue;
    const json::Value* v = table->find(key);
    if (v && v->isString() && !v->string.empty()) {
      entry = v;
      break;
    }
  }
  if (!entry) {
    throw Error(ErrorCode::AdapterUnavailable,
                "no adapter is configured for language '" + language + "' in " + configPath.string());
  }
  fs::path adapter = (configPath.parent_path() / entry->string).lexically_normal();
  AdapterSpec spec;
  spec.path = adapter.string();
  if (isDir(spec.path)) {
    if (isFile((adapter / "Cargo.toml").string())) {
      spec.kind = "rust";
    } else {
      for (const auto& e : fs::directory_iterator(adapter)) {
        if (e.path().extension() == ".csproj") spec.kind = "csharp";
      }
    }
    if (spec.kind.empty()) {
      throw Error(ErrorCode::AdapterUnavailable,
                  "adapter directory has neither Cargo.toml nor a .csproj: " + spec.path);
    }
    return spec;
  }
  if (!isFile(spec.path)) {
    throw Error(ErrorCode::AdapterUnavailable, "configured adapter does not exist: " + spec.path);
  }
  std::string aext = lowerCase(adapter.extension().string());
  if (aext == ".py") spec.kind = "python";
  else if (aext == ".js" || aext == ".mjs" || aext == ".cjs") spec.kind = "node";
  else if (aext == ".go") spec.kind = "go";
  else if (aext == ".java") spec.kind = "java";
  else if (::access(spec.path.c_str(), X_OK) == 0) spec.kind = "exec";
  else {
    throw Error(ErrorCode::AdapterUnavailable,
                "cannot tell how to run adapter (unknown extension, not executable): " + spec.path);
  }
  return spec;
}

}
