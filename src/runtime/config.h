#pragma once

#include <map>
#include <string>

namespace mfdp {

struct AppInfo {
  std::string displayName = "Model File Definition Protocol";
  std::string slug = "MFDP";
  std::string name = "MFDP";
  std::string author;
  std::string version = "1.0.0";
  std::string description;
  std::string license;
};

struct AdapterSpec {
  std::string path;
  std::string kind;
};

std::string findRoot();
std::map<std::string, std::string> parseEnvText(const std::string& text);
std::string envValue(const std::string& key, const std::string& fallback);
AppInfo loadAppInfo();
AdapterSpec resolveAdapter(const std::string& language, const std::string& filename);

}
