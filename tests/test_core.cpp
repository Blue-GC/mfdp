#include <zlib.h>

#include <regex>

#include "test_util.h"

using namespace mfdp;

TEST(sha256_known_vectors)
{
  CHECK_EQ(toHex(sha256("")), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(toHex(sha256("abc")), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  CHECK_EQ(toHex(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
           std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
  std::string million(1000000, 'a');
  CHECK_EQ(toHex(sha256(million)), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(language_map_marks_implemented_and_mapped)
{
  const LanguageEntry *py = findLanguageByExtension(".py");
  const LanguageEntry *ts = findLanguageByExtension(".tsx");
  const LanguageEntry *js = findLanguageByExtension(".MJS");
  const LanguageEntry *go = findLanguageByExtension(".go");
  CHECK(py && py->implemented);
  CHECK(ts && ts->implemented && ts->id == "typescript");
  CHECK(js && js->implemented && js->id == "javascript");
  const LanguageEntry *rust = findLanguageByExtension(".rb");
  CHECK(go && go->implemented);
  CHECK(rust && !rust->implemented);
  CHECK(findLanguageByExtension(".hpp") != nullptr);
  CHECK(findLanguageByExtension(".svelte") != nullptr);
  CHECK(findLanguageByExtension(".nope") == nullptr);
}

TEST(unsupported_language_is_rejected)
{
  CHECK_THROWS_CODE(write(baseOptions({fixture("notes.rb")})), UnsupportedLanguage);
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.lang = "ruby";
  CHECK_THROWS_CODE(buildArtifacts(o), UnsupportedLanguage);
  o.lang = "klingon";
  CHECK_THROWS_CODE(buildArtifacts(o), UnsupportedLanguage);
}

TEST(language_override_cannot_bypass_parser)
{
  WriteOptions o = baseOptions({fixture("sample.ts")});
  o.lang = "python";
  CHECK_THROWS_CODE(buildArtifacts(o), ParseFailed);
  WriteOptions j = baseOptions({fixture("ts_in_js.js")});
  CHECK_THROWS_CODE(buildArtifacts(j), ParseFailed);
}

TEST(malformed_sources_report_structured_errors)
{
  try
  {
    buildArtifacts(baseOptions({fixture("bad.py")}));
    CHECK(false);
  }
  catch (const Error &e)
  {
    CHECK(e.code() == ErrorCode::ParseFailed);
    CHECK_EQ(e.line(), 1u);
    CHECK(contains(e.file(), "bad.py"));
  }
  try
  {
    buildArtifacts(baseOptions({fixture("bad.ts")}));
    CHECK(false);
  }
  catch (const Error &e)
  {
    CHECK(e.code() == ErrorCode::ParseFailed);
    CHECK(e.line() >= 1u);
  }
}

TEST(split_list_handles_commas_quotes_and_escapes)
{
  auto a = splitList("a.py,b.py");
  CHECK_EQ(a.size(), 2u);
  auto b = splitList("\"dir, x/a.py\",b.py");
  CHECK_EQ(b.size(), 2u);
  CHECK_EQ(b[0], std::string("dir, x/a.py"));
  auto c = splitList("dir\\,x/a.py,b.py");
  CHECK_EQ(c.size(), 2u);
  CHECK_EQ(c[0], std::string("dir,x/a.py"));
  CHECK_THROWS_CODE(splitList("\"oops"), InvalidArgument);
  CHECK_EQ(baseName("a/b/c.py"), std::string("c.py"));
}

TEST(roundtrip_and_deterministic_serialization)
{
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "total,calculate_total";
  std::vector<uint8_t> a = serialize(buildArtifacts(o)[0]);
  std::vector<uint8_t> b = serialize(buildArtifacts(o)[0]);
  CHECK(a == b);
  Artifact back = deserialize(a);
  CHECK(serialize(back) == a);
  CHECK_EQ(back.files.size(), 1u);
  CHECK_EQ(back.files[0].filename, std::string("sample.py"));
  CHECK(a[0] == 0x89 && a[1] == 'M');
}

TEST(corrupted_and_truncated_artifacts_are_rejected)
{
  std::vector<uint8_t> good = serialize(buildArtifacts(baseOptions({fixture("sample.py")}))[0]);
  for (size_t cut : {size_t(0), size_t(5), size_t(40), size_t(71), size_t(72), good.size() / 2, good.size() - 1})
  {
    std::vector<uint8_t> t(good.begin(), good.begin() + cut);
    bool threw = false;
    try
    {
      deserialize(t);
    }
    catch (const Error &e)
    {
      threw = true;
      CHECK(e.code() == ErrorCode::TruncatedArtifact || e.code() == ErrorCode::CorruptArtifact);
    }
    CHECK(threw);
  }
  for (size_t pos : {size_t(0), size_t(9), size_t(13), size_t(25), size_t(40), size_t(80), good.size() - 3})
  {
    std::vector<uint8_t> t = good;
    t[pos] ^= 0x55;
    bool threw = false;
    try
    {
      deserialize(t);
    }
    catch (const Error &)
    {
      threw = true;
    }
    CHECK(threw);
  }
  std::vector<uint8_t> extra = good;
  extra.push_back(0);
  CHECK_THROWS_CODE(deserialize(extra), CorruptArtifact);
}

TEST(unsupported_version_and_oversized_payload_rejected)
{
  std::vector<uint8_t> good = serialize(buildArtifacts(baseOptions({fixture("sample.py")}))[0]);
  std::vector<uint8_t> v = good;
  v[8] = 9;
  uint32_t crc = 0;
  {
    uLong c = crc32(0L, v.data(), 68);
    crc = static_cast<uint32_t>(c);
  }
  for (int i = 0; i < 4; ++i)
    v[68 + i] = static_cast<uint8_t>(crc >> (8 * i));
  CHECK_THROWS_CODE(deserialize(v), UnsupportedVersion);
  ReadLimits tiny;
  tiny.maxPayloadBytes = 16;
  CHECK_THROWS_CODE(deserialize(good, tiny), TooLarge);
  ReadLimits tinyFile;
  tinyFile.maxFileBytes = 16;
  CHECK_THROWS_CODE(deserialize(good, tinyFile), TooLarge);
}

TEST(random_output_name_format)
{
  TempDir dir;
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.output_dir = dir.path();
  WriteResult r = write(o);
  CHECK_EQ(r.outputs.size(), 1u);
  std::string name = r.outputs[0].substr(r.outputs[0].find_last_of('/') + 1);
  CHECK(std::regex_match(name, std::regex("[0-9a-f]{32}\\.mfdp")));
  WriteResult r2 = write(o);
  CHECK(r2.outputs[0] != r.outputs[0]);
}

TEST(explicit_output_collision_never_overwrites)
{
  TempDir dir;
  std::string target = dir.file("out.mfdp");
  spit(target, "precious");
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.output = target;
  CHECK_THROWS_CODE(write(o), OutputExists);
  CHECK_EQ(slurpFile(target), std::string("precious"));
  o.overwrite = true;
  write(o);
  CHECK(slurpFile(target) != std::string("precious"));
  Artifact a = read(target);
  CHECK_EQ(a.files.size(), 1u);
}

TEST(output_missing_directory_is_rejected)
{
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.output = "/nonexistent-dir-xyz/out.mfdp";
  CHECK_THROWS_CODE(write(o), Io);
}

TEST(hash_verification_and_stale_detection)
{
  TempDir dir;
  std::string src = dir.file("mod.py");
  spit(src, "def f():\n    return 1\n");
  WriteOptions o = baseOptions({src});
  o.output_dir = dir.path();
  WriteResult r = write(o);
  VerifyReport ok = verify(r.outputs[0], {src});
  CHECK(ok.ok);
  CHECK(ok.entries[0].status == VerifyStatus::Match);
  requireFresh(ok);
  spit(src, "def f():\n    return 2\n");
  VerifyReport stale = verify(r.outputs[0], {src});
  CHECK(!stale.ok);
  CHECK(stale.entries[0].status == VerifyStatus::Stale);
  CHECK_THROWS_CODE(requireFresh(stale), StaleSource);
  VerifyReport missing = verify(r.outputs[0], {});
  CHECK(!missing.ok);
  CHECK(missing.entries[0].status == VerifyStatus::MissingOriginal);
}

TEST(hash_disabled_cannot_be_verified)
{
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.hash = false;
  Artifact a = buildArtifacts(o)[0];
  CHECK(!a.files[0].hasHash);
  CHECK((a.meta.flags & kFlagHashes) == 0u);
  Artifact back = deserialize(serialize(a));
  OriginalSource orig;
  orig.path = fixture("sample.py");
  orig.content = slurpFile(orig.path);
  VerifyReport rep = verify(back, {orig});
  CHECK(!rep.ok);
  CHECK(rep.entries[0].status == VerifyStatus::NoHashStored);
}

TEST(estimate_tokens_is_monotonic)
{
  CHECK(estimateTokens("") == 0);
  CHECK(estimateTokens("def foo(a, b):") < estimateTokens("def foo(a, b):\n    return a + b\n"));
}
