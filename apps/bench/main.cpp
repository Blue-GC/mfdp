#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "mfdp/mfdp.h"

namespace {

using namespace mfdp;
namespace fs = std::filesystem;

struct Row {
  std::string path;
  uint64_t original = 0;
  uint64_t context = 0;
  uint32_t symbolsKept = 0;
  uint32_t symbolsParsed = 0;
  uint32_t fragments = 0;
  uint64_t omitted = 0;
};

std::string slurp(const std::string& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

void collect(const std::string& p, std::vector<std::string>& out) {
  if (fs::is_directory(p)) {
    std::vector<std::string> found;
    for (auto& e : fs::recursive_directory_iterator(p)) {
      if (!e.is_regular_file()) continue;
      const LanguageEntry* le = findLanguageByExtension(e.path().extension().string());
      if (le && le->implemented) found.push_back(e.path().generic_string());
    }
    std::sort(found.begin(), found.end());
    for (auto& f : found) out.push_back(f);
  } else {
    out.push_back(p);
  }
}

}

int main(int argc, char** argv) {
  try {
    std::vector<std::string> paths;
    std::string kwrds = "*";
    Mode mode = Mode::Auto;
    Expand expand = Expand::Auto;
    uint32_t budget = 0;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--kwrds" && i + 1 < argc) kwrds = argv[++i];
      else if (a == "--budget" && i + 1 < argc) budget = static_cast<uint32_t>(std::stoul(argv[++i]));
      else if (a == "--mode" && i + 1 < argc) {
        if (!parseMode(argv[++i], mode)) throw Error(ErrorCode::InvalidArgument, "invalid --mode");
      } else if (a == "--expand-source") expand = Expand::Always;
      else collect(a, paths);
    }
    if (paths.empty()) {
      std::cerr << "usage: mfdp-bench [--kwrds LIST] [--mode M] [--budget N] [--expand-source] PATH...\n";
      return 1;
    }
    std::vector<Row> rows;
    Row total;
    total.path = "TOTAL";
    for (const auto& p : paths) {
      WriteOptions wo;
      wo.src = {p};
      wo.kwrds = kwrds;
      wo.mode = mode;
      wo.budget = budget;
      std::vector<Artifact> arts = buildArtifacts(wo);
      ExtractOptions eo;
      eo.kwrds = "*";
      eo.expand = expand;
      ExtractResult er = extract(arts[0], eo);
      Row r;
      r.path = p;
      r.original = estimateTokens(slurp(p));
      r.context = er.tokens;
      for (const auto& f : arts[0].files) {
        r.symbolsKept += static_cast<uint32_t>(f.symbols.size());
        r.symbolsParsed += f.totalSymbols;
      }
      r.fragments = er.fragmentsIncluded;
      r.omitted = r.original > r.context ? r.original - r.context : 0;
      rows.push_back(r);
      total.original += r.original;
      total.context += r.context;
      total.symbolsKept += r.symbolsKept;
      total.symbolsParsed += r.symbolsParsed;
      total.fragments += r.fragments;
      total.omitted += r.omitted;
    }
    rows.push_back(total);
    std::cout << "token counts are approximations (estimator: word-piece heuristic, not a model tokenizer)\n";
    std::cout << std::left << std::setw(44) << "file" << std::right << std::setw(10) << "orig"
              << std::setw(10) << "context" << std::setw(10) << "reduce%" << std::setw(12) << "symbols"
              << std::setw(11) << "fragments" << std::setw(12) << "omitted~" << "\n";
    for (const auto& r : rows) {
      double pct = r.original ? 100.0 * (1.0 - static_cast<double>(r.context) / static_cast<double>(r.original)) : 0.0;
      std::string name = r.path.size() > 43 ? "..." + r.path.substr(r.path.size() - 40) : r.path;
      std::ostringstream sym;
      sym << r.symbolsKept << "/" << r.symbolsParsed;
      std::cout << std::left << std::setw(44) << name << std::right << std::setw(10) << r.original
                << std::setw(10) << r.context << std::setw(9) << std::fixed << std::setprecision(1) << pct
                << "%" << std::setw(12) << sym.str() << std::setw(11) << r.fragments << std::setw(12)
                << r.omitted << "\n";
    }
    return 0;
  } catch (const Error& e) {
    std::cerr << "error " << e.describe() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }
}
