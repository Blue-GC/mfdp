#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "mfdp/mfdp.h"

namespace mfdp {

struct RawSymbol {
  Symbol sym;
  std::vector<std::string> calls;
  std::vector<std::string> refs;
  bool defaultExport = false;
};

struct ParsedFile {
  std::string path;
  std::string filename;
  std::string language;
  std::string content;
  Hash hash{};
  std::vector<Import> imports;
  std::vector<RawSymbol> symbols;
  std::vector<std::string> exports;
};

ParsedFile parseSource(const std::string& path, const std::string& filename,
                       const std::string& content, const std::string& language);

std::string runAdapterProcess(const std::string& language, const std::string& filename,
                              const std::string& content);

}
