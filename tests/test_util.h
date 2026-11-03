#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "mfdp/mfdp.h"
#include "test_framework.h"

#ifndef MFDP_TEST_DIR
#define MFDP_TEST_DIR "tests"
#endif

inline std::string fixture(const std::string& name) { return std::string(MFDP_TEST_DIR) + "/fixtures/" + name; }

inline std::string slurpFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

inline void spit(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary);
  out << data;
}

class TempDir {
 public:
  TempDir() {
    char tmpl[] = "/tmp/mfdp-test-XXXXXX";
    char* p = ::mkdtemp(tmpl);
    path_ = p ? p : "/tmp";
  }
  ~TempDir() {
    std::string cmd = "rm -rf '" + path_ + "'";
    int rc = std::system(cmd.c_str());
    (void)rc;
  }
  const std::string& path() const { return path_; }
  std::string file(const std::string& name) const { return path_ + "/" + name; }

 private:
  std::string path_;
};

inline std::vector<std::string> listDir(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = ::opendir(dir.c_str());
  if (!d) return out;
  while (dirent* e = ::readdir(d)) {
    std::string n = e->d_name;
    if (n != "." && n != "..") out.push_back(n);
  }
  ::closedir(d);
  return out;
}

inline mfdp::WriteOptions baseOptions(const std::vector<std::string>& src) {
  mfdp::WriteOptions o;
  o.src = src;
  return o;
}

inline const mfdp::Symbol* findSymbol(const mfdp::FileRecord& f, const std::string& qualified) {
  for (const auto& s : f.symbols) {
    if (s.qualified == qualified) return &s;
  }
  return nullptr;
}

inline bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}
