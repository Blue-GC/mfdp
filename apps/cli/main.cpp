#include <iostream>
#include <sstream>
#include <map>

#include "config.h"
#include "mfdp/mfdp.h"
#include "rpc.h"

namespace {

using namespace mfdp;

void usage() {
  std::cerr <<
      "usage: mfdp <command> [options]\n"
      "commands:\n"
      "  write    --src PATHS [--kwrds LIST] [--inc B] [--include B] [--inctyps B] [--include-types B]\n"
      "           [--import-mode all|relevant|none] [--legacy-inctyps B] [--budget N] [--lang ID]\n"
      "           [--hash B] [--mode auto|symbols|full] [--deps B] [--dep-depth N] [--lines B]\n"
      "           [--combined B] [--output PATHS] [--output-dir DIR] [--overwrite B]\n"
      "  read     ARTIFACT\n"
      "  inspect  ARTIFACT\n"
      "  extract  ARTIFACT [--kwrds LIST] [--budget N] [--expand auto|always|never] [--deps B] [--imports B]\n"
      "  verify   ARTIFACT --src PATHS\n"
      "  rpc      read one JSON request from stdin, print one JSON response (used by the bindings and the HTTP API)\n"
      "  version  print application information\n"
      "boolean values: true|false|1|0|yes|no\n";
}

struct Args {
  std::string command;
  std::vector<std::string> positional;
  std::map<std::string, std::vector<std::string>> options;
};

Args parseArgs(int argc, char** argv) {
  Args a;
  if (argc < 2) return a;
  a.command = argv[1];
  for (int i = 2; i < argc; ++i) {
    std::string t = argv[i];
    if (t.size() > 2 && t.compare(0, 2, "--") == 0) {
      std::string key = t.substr(2);
      std::string value;
      size_t eq = key.find('=');
      if (eq != std::string::npos) {
        value = key.substr(eq + 1);
        key = key.substr(0, eq);
      } else if (key.compare(0, 3, "no-") == 0) {
        key = key.substr(3);
        value = "false";
      } else if (i + 1 < argc) {
        value = argv[++i];
      } else {
        throw Error(ErrorCode::InvalidArgument, "option --" + key + " requires a value");
      }
      for (char& c : key) {
        if (c == '-') c = '_';
      }
      a.options[key].push_back(value);
    } else {
      a.positional.push_back(t);
    }
  }
  return a;
}

bool boolOf(const std::string& key, const std::string& v) {
  bool out = false;
  if (!parseBool(v, out)) throw Error(ErrorCode::InvalidArgument, "option --" + key + " expects a boolean");
  return out;
}

uint32_t uintOf(const std::string& key, const std::string& v) {
  try {
    size_t pos = 0;
    unsigned long n = std::stoul(v, &pos);
    if (pos != v.size() || n > 0xFFFFFFFFul) throw std::invalid_argument("x");
    return static_cast<uint32_t>(n);
  } catch (...) {
    throw Error(ErrorCode::InvalidArgument, "option --" + key + " expects a non-negative integer");
  }
}

const std::string* last(const Args& a, const std::string& key) {
  auto it = a.options.find(key);
  if (it == a.options.end() || it->second.empty()) return nullptr;
  return &it->second.back();
}

std::vector<std::string> sources(const Args& a) {
  std::vector<std::string> out;
  auto it = a.options.find("src");
  if (it != a.options.end()) {
    for (const auto& v : it->second) {
      for (auto& p : splitList(v)) out.push_back(p);
    }
  }
  return out;
}

void printWarnings(const std::vector<Warning>& ws) {
  for (const auto& w : ws) std::cerr << "warning[" << w.code << "]: " << w.message << "\n";
}

int runWrite(const Args& a) {
  WriteOptions o;
  o.src = sources(a);
  for (const auto& p : a.positional) o.src.push_back(p);
  if (const std::string* v = last(a, "kwrds")) o.kwrds = *v;
  if (const std::string* v = last(a, "inc")) o.inc = boolOf("inc", *v);
  if (const std::string* v = last(a, "include")) o.include = boolOf("include", *v);
  if (const std::string* v = last(a, "inctyps")) o.inctyps = boolOf("inctyps", *v);
  if (const std::string* v = last(a, "include_types")) o.include_types = boolOf("include-types", *v);
  if (const std::string* v = last(a, "legacy_inctyps")) o.legacy_inctyps = boolOf("legacy-inctyps", *v);
  if (const std::string* v = last(a, "import_mode")) {
    ImportMode m;
    if (!parseImportMode(*v, m)) throw Error(ErrorCode::InvalidArgument, "invalid --import-mode");
    o.import_mode = m;
  }
  if (const std::string* v = last(a, "budget")) o.budget = uintOf("budget", *v);
  if (const std::string* v = last(a, "lang")) o.lang = *v;
  if (const std::string* v = last(a, "hash")) o.hash = boolOf("hash", *v);
  if (const std::string* v = last(a, "mode")) {
    if (!parseMode(*v, o.mode)) throw Error(ErrorCode::InvalidArgument, "invalid --mode");
  }
  if (const std::string* v = last(a, "deps")) o.deps = boolOf("deps", *v);
  if (const std::string* v = last(a, "dep_depth")) o.dep_depth = uintOf("dep-depth", *v);
  if (const std::string* v = last(a, "lines")) o.lines = boolOf("lines", *v);
  if (const std::string* v = last(a, "combined")) o.combined = boolOf("combined", *v);
  if (const std::string* v = last(a, "output")) o.output = *v;
  if (const std::string* v = last(a, "output_dir")) o.output_dir = *v;
  if (const std::string* v = last(a, "overwrite")) o.overwrite = boolOf("overwrite", *v);
  WriteResult r = write(o);
  printWarnings(r.warnings);
  for (const auto& s : r.summaries) {
    std::cout << s.output << "\n";
    std::cerr << "  files=" << s.files << " symbols=" << s.symbolsStored << "/" << s.symbolsParsed
              << " bytes=" << s.storedBytes << " (payload " << s.rawBytes << ")\n";
  }
  return 0;
}

int runRead(const Args& a) {
  if (a.positional.size() != 1) throw Error(ErrorCode::InvalidArgument, "read expects one artifact path");
  Artifact art = read(a.positional[0]);
  size_t symbols = 0;
  for (const auto& f : art.files) symbols += f.symbols.size();
  std::cout << "valid mfdp " << art.meta.versionMajor << "." << art.meta.versionMinor << " files="
            << art.files.size() << " symbols=" << symbols << "\n";
  return 0;
}

int runInspect(const Args& a) {
  if (a.positional.size() != 1) throw Error(ErrorCode::InvalidArgument, "inspect expects one artifact path");
  std::cout << formatInspect(inspect(a.positional[0]));
  return 0;
}

int runExtract(const Args& a) {
  if (a.positional.size() != 1) throw Error(ErrorCode::InvalidArgument, "extract expects one artifact path");
  ExtractOptions o;
  if (const std::string* v = last(a, "kwrds")) o.kwrds = *v;
  if (const std::string* v = last(a, "budget")) o.budget = uintOf("budget", *v);
  if (const std::string* v = last(a, "deps")) o.deps = boolOf("deps", *v);
  if (const std::string* v = last(a, "imports")) o.imports = boolOf("imports", *v);
  if (const std::string* v = last(a, "expand")) {
    if (*v == "auto") o.expand = Expand::Auto;
    else if (*v == "always") o.expand = Expand::Always;
    else if (*v == "never") o.expand = Expand::Never;
    else throw Error(ErrorCode::InvalidArgument, "invalid --expand");
  }
  ExtractResult r = extract(a.positional[0], o);
  std::cout << r.text;
  printWarnings(r.warnings);
  std::cerr << "tokens~" << r.tokens << " (approximate)\n";
  return 0;
}

int runVerify(const Args& a) {
  if (a.positional.size() != 1) throw Error(ErrorCode::InvalidArgument, "verify expects one artifact path");
  std::vector<std::string> paths = sources(a);
  if (paths.empty()) throw Error(ErrorCode::InvalidArgument, "verify requires --src");
  VerifyReport r = verify(a.positional[0], paths);
  std::cout << formatVerify(r);
  return r.ok ? 0 : 3;
}

}

int main(int argc, char** argv) {
  try {
    Args a = parseArgs(argc, argv);
    if (a.command.empty() || a.command == "--help" || a.command == "help") {
      usage();
      return a.command.empty() ? 1 : 0;
    }
    if (a.command == "write") return runWrite(a);
    if (a.command == "read") return runRead(a);
    if (a.command == "inspect") return runInspect(a);
    if (a.command == "extract") return runExtract(a);
    if (a.command == "verify") return runVerify(a);
    if (a.command == "rpc") {
      std::ostringstream in;
      in << std::cin.rdbuf();
      std::cout << handleRpc(in.str()) << "\n";
      return 0;
    }
    if (a.command == "version" || a.command == "--version") {
      AppInfo info = loadAppInfo();
      std::cout << info.displayName << " (" << info.name << ") " << info.version << ", format 1.0\n";
      return 0;
    }
    usage();
    return 1;
  } catch (const Error& e) {
    std::cerr << "error " << e.describe() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
