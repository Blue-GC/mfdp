#include "test_util.h"

#ifndef MFDP_CLI_PATH
#define MFDP_CLI_PATH "mfdp"
#endif

namespace {

int run(const std::string& args) {
  std::string cmd = std::string("'") + MFDP_CLI_PATH + "' " + args + " >/dev/null 2>&1";
  int rc = std::system(cmd.c_str());
  return WEXITSTATUS(rc);
}

}

TEST(cli_write_inspect_extract_verify_roundtrip) {
  TempDir dir;
  std::string src = dir.file("m.py");
  spit(src, "def f(a):\n    return a\n");
  std::string out = dir.file("m.mfdp");
  CHECK_EQ(run("write --src '" + src + "' --kwrds f --output '" + out + "'"), 0);
  CHECK_EQ(run("read '" + out + "'"), 0);
  CHECK_EQ(run("inspect '" + out + "'"), 0);
  CHECK_EQ(run("extract '" + out + "' --kwrds f --budget 200"), 0);
  CHECK_EQ(run("verify '" + out + "' --src '" + src + "'"), 0);
  spit(src, "def f(a):\n    return a + 1\n");
  CHECK_EQ(run("verify '" + out + "' --src '" + src + "'"), 3);
  CHECK_EQ(run("write --src '" + src + "' --output '" + out + "'"), 2);
  CHECK_EQ(run("write --src '" + src + "' --inc true --include false"), 2);
  CHECK_EQ(run("write --src '" + src + "' --no-lines --no-hash --output-dir '" + dir.path() + "'"), 0);
  CHECK_EQ(run("bogus"), 1);
}

TEST(cli_rejects_garbage_artifacts) {
  TempDir dir;
  std::string bad = dir.file("bad.mfdp");
  spit(bad, "this is not a mfdp artifact at all, just text that is long enough to have a header");
  CHECK_EQ(run("read '" + bad + "'"), 2);
  CHECK_EQ(run("inspect '" + bad + "'"), 2);
}

TEST(cli_rpc_round_trip) {
  TempDir dir;
  std::string request = dir.file("request.json");
  std::string response = dir.file("response.json");
  spit(request,
       "{\"method\":\"write\",\"params\":{\"memory\":[{\"filename\":\"a.py\",\"content\":\"def f():\\n    return 1\\n\"}],"
       "\"output_dir\":\"" + dir.path() + "\"}}");
  std::string cmd = std::string("'") + MFDP_CLI_PATH + "' rpc < '" + request + "' > '" + response + "'";
  CHECK_EQ(std::system(cmd.c_str()), 0);
  std::string out = slurpFile(response);
  CHECK(contains(out, "\"ok\":true"));
  CHECK(contains(out, ".mfdp"));
  spit(request, "{not json");
  CHECK_EQ(std::system(cmd.c_str()), 0);
  out = slurpFile(response);
  CHECK(contains(out, "\"ok\":false"));
  CHECK(contains(out, "InvalidArgument"));
}
