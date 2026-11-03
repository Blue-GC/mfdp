#include <cstdlib>

#include "config.h"
#include "test_util.h"

using namespace mfdp;

TEST(env_parser_handles_comments_quotes_and_trailing_backslash) {
  auto v = parseEnvText(
      "# header\n"
      "APP_INFO=./config/config.json # inline comment\n"
      "export PORT=3847\n"
      "QUOTED=\"keep # this\"\n"
      "SINGLE='a b'\n"
      "SECRET=abc123==\\\n"
      "EMPTY=\n"
      "no equals line\n");
  CHECK_EQ(v["APP_INFO"], std::string("./config/config.json"));
  CHECK_EQ(v["PORT"], std::string("3847"));
  CHECK_EQ(v["QUOTED"], std::string("keep # this"));
  CHECK_EQ(v["SINGLE"], std::string("a b"));
  CHECK_EQ(v["SECRET"], std::string("abc123=="));
  CHECK_EQ(v["EMPTY"], std::string(""));
  CHECK(v.find("no equals line") == v.end());
}

TEST(adapters_are_resolved_from_configuration_files) {
  AdapterSpec py = resolveAdapter("python", "a.py");
  CHECK_EQ(py.kind, std::string("python"));
  CHECK(contains(py.path, "python_adapter.py"));
  CHECK_EQ(resolveAdapter("typescript", "a.tsx").kind, std::string("node"));
  CHECK_EQ(resolveAdapter("javascript", "a.js").kind, std::string("node"));
  CHECK_EQ(resolveAdapter("go", "a.go").kind, std::string("go"));
  CHECK_EQ(resolveAdapter("java", "A.java").kind, std::string("java"));
  CHECK_EQ(resolveAdapter("rust", "a.rs").kind, std::string("rust"));
  CHECK_EQ(resolveAdapter("csharp", "a.cs").kind, std::string("csharp"));
  AdapterSpec header = resolveAdapter("c", "util.h");
  CHECK_EQ(header.kind, std::string("python"));
  CHECK(contains(header.path, "clang_adapter.py"));
}

TEST(language_without_a_configured_adapter_is_reported) {
  CHECK_THROWS_CODE(resolveAdapter("yaml", "a.yaml"), AdapterUnavailable);
}

TEST(adapter_set_name_cannot_escape_the_adapters_directory) {
  ::setenv("PROGRAMMING_ADAPTERS", "../evil", 1);
  bool threw = false;
  try {
    resolveAdapter("python", "a.py");
  } catch (const Error& e) {
    threw = e.code() == ErrorCode::InvalidArgument;
  }
  ::unsetenv("PROGRAMMING_ADAPTERS");
  CHECK(threw);
  ::setenv("PROGRAMMING_ADAPTERS", "missing_set", 1);
  threw = false;
  try {
    resolveAdapter("python", "a.py");
  } catch (const Error& e) {
    threw = e.code() == ErrorCode::AdapterUnavailable;
  }
  ::unsetenv("PROGRAMMING_ADAPTERS");
  CHECK(threw);
}

TEST(application_info_comes_from_config_json) {
  AppInfo info = loadAppInfo();
  CHECK_EQ(info.name, std::string("MFDP"));
  CHECK_EQ(info.slug, std::string("MFDP"));
  CHECK(!info.version.empty());
}
