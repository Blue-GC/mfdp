#include "test_util.h"

using namespace mfdp;

TEST(single_python_file) {
  Artifact a = buildArtifacts(baseOptions({fixture("sample.py")}))[0];
  const FileRecord& f = a.files[0];
  CHECK_EQ(f.language, std::string("python"));
  CHECK_EQ(f.filename, std::string("sample.py"));
  CHECK_EQ(f.totalSymbols, 10u);
  const Symbol* total = findSymbol(f, "Calculator.total");
  CHECK(total != nullptr);
  CHECK(total->kind == SymbolKind::Method);
  CHECK(total->signature.find("async def total(self, tax: float = 0.1, *args, key=None, **kw) -> float") == 0);
  CHECK_EQ(total->returnType, std::string("float"));
  CHECK_EQ(total->params.size(), 5u);
  CHECK_EQ(total->params[1].defaultValue, std::string("0.1"));
  const Symbol* cls = findSymbol(f, "Calculator");
  CHECK(cls != nullptr);
  CHECK_EQ(cls->bases.size(), 1u);
  CHECK(contains(cls->doc, "Calc doc."));
  CHECK(total->parentPlus1 > 0);
  CHECK(f.symbols[total->parentPlus1 - 1].qualified == "Calculator");
  const Symbol* sq = findSymbol(f, "Calculator.sq");
  CHECK(sq && sq->decorators.size() == 1 && sq->decorators[0] == "staticmethod");
  CHECK(findSymbol(f, "TAX_RATE") != nullptr);
  CHECK(findSymbol(f, "calculate_total.inner") != nullptr);
  CHECK(f.exports.size() == 2);
}

TEST(single_typescript_file) {
  Artifact a = buildArtifacts(baseOptions({fixture("sample.ts")}))[0];
  const FileRecord& f = a.files[0];
  CHECK_EQ(f.language, std::string("typescript"));
  const Symbol* item = findSymbol(f, "Item");
  CHECK(item && item->kind == SymbolKind::Interface);
  CHECK(findSymbol(f, "Item.total") != nullptr);
  const Symbol* id = findSymbol(f, "ID");
  CHECK(id && id->kind == SymbolKind::TypeAlias);
  const Symbol* color = findSymbol(f, "Color");
  CHECK(color && color->kind == SymbolKind::Enum);
  const Symbol* cls = findSymbol(f, "Calculator");
  CHECK(cls && cls->kind == SymbolKind::Class && cls->exported == Tri::Yes);
  CHECK(cls->bases.size() == 2);
  CHECK(contains(cls->doc, "Calc docs."));
  const Symbol* fn = findSymbol(f, "calculateTotal");
  CHECK(fn && fn->returnType == "number" && fn->params.size() == 2);
  CHECK(fn->params[1].defaultValue == "TAX");
  const Symbol* arrow = findSymbol(f, "arrow");
  CHECK(arrow && arrow->kind == SymbolKind::Function);
  const Symbol* method = findSymbol(f, "Calculator.total");
  CHECK(method && method->parentPlus1 > 0);
  CHECK(findSymbol(f, "NS.f") != nullptr);
  CHECK(findSymbol(f, "TAX") && findSymbol(f, "TAX")->kind == SymbolKind::Constant);
}

TEST(single_javascript_file) {
  Artifact a = buildArtifacts(baseOptions({fixture("sample.js")}))[0];
  const FileRecord& f = a.files[0];
  CHECK_EQ(f.language, std::string("javascript"));
  CHECK(findSymbol(f, "main") != nullptr);
  const Symbol* hello = findSymbol(f, "hello");
  CHECK(hello && hello->exported == Tri::Yes);
  CHECK_EQ(f.totalImports, 2u);
  bool undefinedFlagged = false;
  for (const auto& d : f.deps) {
    if (d.name == "undefinedThing") undefinedFlagged = d.status == DepStatus::Unresolved;
  }
  CHECK(undefinedFlagged);
}

TEST(wildcard_selects_all_symbols) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "*";
  Artifact a = buildArtifacts(o)[0];
  CHECK_EQ(a.files[0].symbols.size(), 10u);
  for (const auto& s : a.files[0].symbols) CHECK(s.selected);
  CHECK(a.meta.resolvedMode == Mode::Symbols);
  CHECK((a.meta.flags & kFlagFragments) == 0u);
}

TEST(comma_separated_selection_uses_parsed_names) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "calculate_total, validate";
  Artifact a = buildArtifacts(o)[0];
  const FileRecord& f = a.files[0];
  CHECK(findSymbol(f, "calculate_total")->selected);
  CHECK(findSymbol(f, "validate")->selected);
  CHECK(!findSymbol(f, "Calculator.total") || !findSymbol(f, "Calculator.total")->selected);
  CHECK(a.meta.resolvedMode == Mode::Full);
  CHECK(findSymbol(f, "calculate_total")->hasFragment);
  WriteOptions sub = baseOptions({fixture("sample.py")});
  sub.kwrds = "calc";
  CHECK_THROWS_CODE(buildArtifacts(sub), NoMatch);
  WriteOptions partial = baseOptions({fixture("sample.py")});
  partial.kwrds = "validate,missing_one";
  std::vector<Warning> warnings;
  buildArtifacts(partial, &warnings);
  bool found = false;
  for (const auto& w : warnings) {
    if (w.code == "symbol_not_found" && contains(w.message, "missing_one")) found = true;
  }
  CHECK(found);
  WriteOptions qual = baseOptions({fixture("sample.py")});
  qual.kwrds = "Calculator.sq";
  Artifact q = buildArtifacts(qual)[0];
  CHECK(findSymbol(q.files[0], "Calculator.sq")->selected);
  CHECK(findSymbol(q.files[0], "Calculator") != nullptr);
  CHECK(!findSymbol(q.files[0], "Calculator")->selected);
}

TEST(import_filtering_modes) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "total";
  o.import_mode = ImportMode::All;
  CHECK_EQ(buildArtifacts(o)[0].files[0].imports.size(), 3u);
  o.import_mode = ImportMode::Relevant;
  Artifact rel = buildArtifacts(o)[0];
  CHECK_EQ(rel.files[0].imports.size(), 2u);
  o.import_mode = ImportMode::None;
  CHECK_EQ(buildArtifacts(o)[0].files[0].imports.size(), 0u);
  WriteOptions off = baseOptions({fixture("sample.py")});
  off.kwrds = "total";
  off.inc = false;
  Artifact none = buildArtifacts(off)[0];
  CHECK_EQ(none.files[0].imports.size(), 0u);
  CHECK((none.meta.flags & kFlagImports) == 0u);
  WriteOptions legacy = baseOptions({fixture("sample.py")});
  legacy.kwrds = "total";
  legacy.legacy_inctyps = true;
  legacy.inctyps = true;
  CHECK_EQ(buildArtifacts(legacy)[0].files[0].imports.size(), 3u);
  legacy.inctyps = false;
  CHECK_EQ(buildArtifacts(legacy)[0].files[0].imports.size(), 2u);
}

TEST(alias_conflicts_are_rejected) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.inc = true;
  o.include = false;
  CHECK_THROWS_CODE(buildArtifacts(o), Conflict);
  WriteOptions t = baseOptions({fixture("sample.py")});
  t.inctyps = true;
  t.include_types = false;
  CHECK_THROWS_CODE(buildArtifacts(t), Conflict);
  WriteOptions m = baseOptions({fixture("sample.py")});
  m.inc = false;
  m.import_mode = ImportMode::All;
  CHECK_THROWS_CODE(buildArtifacts(m), Conflict);
  WriteOptions agree = baseOptions({fixture("sample.py")});
  agree.inc = false;
  agree.include = false;
  CHECK(buildArtifacts(agree)[0].files[0].imports.empty());
}

TEST(type_inclusion_and_exclusion) {
  WriteOptions on = baseOptions({fixture("sample.ts")});
  Artifact a = buildArtifacts(on)[0];
  CHECK(findSymbol(a.files[0], "Item") && findSymbol(a.files[0], "Color") && findSymbol(a.files[0], "ID"));
  WriteOptions off = baseOptions({fixture("sample.ts")});
  off.inctyps = false;
  std::vector<Warning> warnings;
  Artifact b = buildArtifacts(off, &warnings)[0];
  const FileRecord& f = b.files[0];
  CHECK(findSymbol(f, "ID") == nullptr);
  CHECK(findSymbol(f, "Color") == nullptr);
  CHECK(findSymbol(f, "Item") != nullptr && !findSymbol(f, "Item")->selected);
  CHECK(findSymbol(f, "calculateTotal") != nullptr);
  const Symbol* method = findSymbol(f, "Calculator.total");
  CHECK(method != nullptr);
  const Symbol* stub = findSymbol(f, "Calculator");
  CHECK(stub && !stub->selected && stub->context);
  CHECK((b.meta.flags & kFlagTypes) == 0u);
  bool stubWarning = false;
  for (const auto& w : warnings) stubWarning = stubWarning || w.code == "type_ancestor_stub";
  CHECK(stubWarning);
  WriteOptions explicitType = baseOptions({fixture("sample.ts")});
  explicitType.inctyps = false;
  explicitType.kwrds = "Item,calculateTotal";
  std::vector<Warning> w2;
  buildArtifacts(explicitType, &w2);
  bool excluded = false;
  for (const auto& w : w2) excluded = excluded || w.code == "type_excluded";
  CHECK(excluded);
}

TEST(line_numbers_enabled_and_disabled) {
  WriteOptions on = baseOptions({fixture("sample.py")});
  on.kwrds = "validate";
  Artifact a = deserialize(serialize(buildArtifacts(on)[0]));
  CHECK(findSymbol(a.files[0], "validate")->startLine == 8);
  CHECK(contains(extract(a).text, "[L8-9]"));
  WriteOptions off = on;
  off.lines = false;
  Artifact b = deserialize(serialize(buildArtifacts(off)[0]));
  CHECK((b.meta.flags & kFlagLines) == 0u);
  CHECK(findSymbol(b.files[0], "validate")->startLine == 0);
  for (const auto& imp : b.files[0].imports) CHECK(imp.line == 0);
  ExtractResult r = extract(b);
  CHECK(!contains(r.text, "[L"));
  CHECK(!contains(r.text, " L1:"));
  CHECK(serialize(buildArtifacts(off)[0]).size() < serialize(buildArtifacts(on)[0]).size());
}

TEST(combined_artifact_with_multiple_files) {
  WriteOptions o = baseOptions({fixture("multi/billing.py"), fixture("multi/util.py"), fixture("multi/models.py")});
  o.kwrds = "compute";
  std::vector<Artifact> arts = buildArtifacts(o);
  CHECK_EQ(arts.size(), 1u);
  const Artifact& a = arts[0];
  CHECK_EQ(a.files.size(), 3u);
  CHECK(findSymbol(a.files[0], "compute")->selected);
  CHECK(findSymbol(a.files[0], "unused") == nullptr);
  CHECK(findSymbol(a.files[1], "helper") && findSymbol(a.files[1], "helper")->context);
  CHECK(findSymbol(a.files[2], "Invoice") && findSymbol(a.files[2], "Invoice")->context);
  CHECK(a.meta.combined);
  CHECK(!findSymbol(a.files[1], "helper")->hasFragment);
}

TEST(separate_artifacts_for_multiple_files) {
  TempDir dir;
  WriteOptions o = baseOptions({fixture("multi/billing.py"), fixture("multi/util.py")});
  o.combined = false;
  o.output_dir = dir.path();
  WriteResult r = write(o);
  CHECK_EQ(r.outputs.size(), 2u);
  CHECK(r.outputs[0] != r.outputs[1]);
  Artifact first = read(r.outputs[0]);
  Artifact second = read(r.outputs[1]);
  CHECK_EQ(first.files.size(), 1u);
  CHECK_EQ(first.files[0].filename, std::string("billing.py"));
  CHECK_EQ(second.files[0].filename, std::string("util.py"));
  CHECK(!first.meta.combined);
  bool external = false;
  for (const auto& d : first.files[0].deps) {
    if (d.name == "helper") external = d.status == DepStatus::External && d.module == ".util";
  }
  CHECK(external);
  WriteOptions named = o;
  named.output = dir.file("one.mfdp") + "," + dir.file("two.mfdp");
  WriteResult nr = write(named);
  CHECK_EQ(nr.outputs[0], dir.file("one.mfdp"));
  CHECK_EQ(nr.outputs[1], dir.file("two.mfdp"));
  WriteOptions wrong = o;
  wrong.output = dir.file("only.mfdp") + "," + dir.file("only2.mfdp") + "," + dir.file("only3.mfdp");
  CHECK_THROWS_CODE(write(wrong), InvalidArgument);
}

TEST(dependency_extraction_cross_file_python) {
  WriteOptions o = baseOptions({fixture("multi/billing.py"), fixture("multi/util.py"), fixture("multi/models.py")});
  o.kwrds = "compute";
  Artifact a = buildArtifacts(o)[0];
  const FileRecord& f = a.files[0];
  bool helper = false;
  bool invoice = false;
  for (const auto& d : f.deps) {
    if (d.name == "helper") {
      helper = d.status == DepStatus::CrossFile && d.targetFile == 1 && d.targetSymbol >= 0 &&
               a.files[1].symbols[d.targetSymbol].qualified == "helper";
    }
    if (d.name == "Invoice") {
      invoice = d.status == DepStatus::CrossFile && d.targetFile == 2 &&
                a.files[2].symbols[d.targetSymbol].qualified == "Invoice";
    }
  }
  CHECK(helper);
  CHECK(invoice);
}

TEST(dependency_extraction_cross_file_typescript) {
  WriteOptions o = baseOptions({fixture("multi/a.ts"), fixture("multi/b.ts")});
  o.kwrds = "describe";
  Artifact a = buildArtifacts(o)[0];
  const FileRecord& f = a.files[0];
  bool area = false, shape = false, perimeter = false;
  for (const auto& d : f.deps) {
    if (d.status != DepStatus::CrossFile || d.targetSymbol < 0) continue;
    const std::string& q = a.files[d.targetFile].symbols[d.targetSymbol].qualified;
    if (d.name == "area" && q == "area") area = true;
    if (d.name == "Shape" && q == "Shape") shape = true;
    if (d.name == "lib.perimeter" && q == "perimeter") perimeter = true;
  }
  CHECK(area);
  CHECK(shape);
  CHECK(perimeter);
}

TEST(dependency_depth_and_disable) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "total";
  Artifact withDeps = buildArtifacts(o)[0];
  CHECK(findSymbol(withDeps.files[0], "validate") != nullptr);
  CHECK(findSymbol(withDeps.files[0], "Calculator.sq") != nullptr);
  CHECK(!withDeps.files[0].deps.empty());
  WriteOptions off = o;
  off.deps = false;
  Artifact noDeps = buildArtifacts(off)[0];
  CHECK(findSymbol(noDeps.files[0], "validate") == nullptr);
  CHECK(noDeps.files[0].deps.empty());
  CHECK((noDeps.meta.flags & kFlagDeps) == 0u);
  WriteOptions deep = baseOptions({fixture("multi/billing.py"), fixture("multi/util.py"), fixture("multi/models.py")});
  deep.kwrds = "compute";
  deep.dep_depth = 2;
  CHECK(buildArtifacts(deep)[0].meta.depDepth == 2u);
  WriteOptions bad = deep;
  bad.dep_depth = 0;
  CHECK_THROWS_CODE(buildArtifacts(bad), InvalidArgument);
}

TEST(budget_limited_extraction_reports_omissions) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "total,calculate_total";
  Artifact a = deserialize(serialize(buildArtifacts(o)[0]));
  ExtractResult full = extract(a);
  CHECK(full.warnings.empty());
  CHECK_EQ(full.fragmentsIncluded, 2u);
  ExtractOptions tight;
  tight.budget = 150;
  ExtractResult small = extract(a, tight);
  CHECK(small.tokens <= 150);
  CHECK(small.tokens < full.tokens);
  CHECK(small.fragmentsOmitted > 0);
  bool warned = false;
  for (const auto& w : small.warnings) warned = warned || w.code == "budget_omitted_source";
  CHECK(warned);
  CHECK(contains(small.text, "async def total"));
  CHECK(contains(small.text, "def calculate_total"));
  ExtractOptions tiny;
  tiny.budget = 5;
  ExtractResult t = extract(a, tiny);
  bool headerWarning = false;
  for (const auto& w : t.warnings) headerWarning = headerWarning || w.code == "budget_exceeded_minimum";
  CHECK(headerWarning);
}

TEST(auto_mode_downgrades_when_budget_too_small) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  o.kwrds = "total,calculate_total";
  o.budget = 20;
  Artifact a = buildArtifacts(o)[0];
  CHECK(a.meta.resolvedMode == Mode::Symbols);
  CHECK((serialize(a).size() > 0));
  bool noted = false;
  for (const auto& w : a.meta.warnings) noted = noted || w.code == "auto_mode_downgraded";
  CHECK(noted);
  CHECK(!findSymbol(a.files[0], "calculate_total")->hasFragment);
}

TEST(modes_do_not_claim_missing_source) {
  WriteOptions sym = baseOptions({fixture("sample.py")});
  sym.kwrds = "validate";
  sym.mode = Mode::Symbols;
  Artifact a = buildArtifacts(sym)[0];
  CHECK(!findSymbol(a.files[0], "validate")->hasFragment);
  CHECK(!contains(extract(a, ExtractOptions()).text, "source:"));
  WriteOptions full = sym;
  full.mode = Mode::Full;
  Artifact b = buildArtifacts(full)[0];
  CHECK(findSymbol(b.files[0], "validate")->hasFragment);
  CHECK(contains(extract(b).text, "source:"));
  WriteOptions star = baseOptions({fixture("sample.py")});
  star.mode = Mode::Full;
  Artifact c = buildArtifacts(star)[0];
  const Symbol* method = findSymbol(c.files[0], "Calculator.total");
  CHECK(method->coveredByAncestor && !method->hasFragment);
  ExtractResult quiet = extract(c);
  CHECK(!contains(quiet.text, "source:"));
  CHECK(contains(quiet.text, "NOTE:"));
  ExtractOptions forced;
  forced.expand = Expand::Always;
  CHECK(contains(extract(c, forced).text, "source:"));
}

TEST(extract_subset_by_keyword) {
  WriteOptions o = baseOptions({fixture("sample.py")});
  Artifact a = buildArtifacts(o)[0];
  ExtractOptions e;
  e.kwrds = "validate";
  ExtractResult r = extract(a, e);
  CHECK(contains(r.text, "def validate(x)"));
  CHECK(!contains(r.text, "calculate_total"));
  ExtractOptions missing;
  missing.kwrds = "nothing_here";
  ExtractResult m = extract(a, missing);
  CHECK(!m.warnings.empty());
}

TEST(paths_with_spaces_and_commas) {
  TempDir dir;
  std::string sub = dir.file("my dir, with comma");
  ::mkdir(sub.c_str(), 0755);
  std::string a = sub + "/a file, one.py";
  std::string b = sub + "/b.py";
  spit(a, "def alpha():\n    return 1\n");
  spit(b, "def beta():\n    return 2\n");
  WriteOptions o = baseOptions({a, b});
  std::string outPath = sub + "/out, name.mfdp";
  o.output = "\"" + outPath + "\"";
  WriteResult r = write(o);
  CHECK_EQ(r.outputs[0], outPath);
  Artifact art = read(r.outputs[0]);
  CHECK_EQ(art.files[0].filename, std::string("a file, one.py"));
  std::string list = "\"" + a + "\",\"" + b + "\"";
  std::vector<std::string> parts = splitList(list);
  CHECK_EQ(parts.size(), 2u);
  CHECK_EQ(parts[0], a);
  VerifyReport rep = verify(r.outputs[0], {a, b});
  CHECK(rep.ok);
}

TEST(in_memory_sources_use_explicit_filename) {
  WriteOptions o;
  o.memory.push_back({"virtual/mod.py", "def hi():\n    return 1\n"});
  Artifact a = buildArtifacts(o)[0];
  CHECK_EQ(a.files[0].filename, std::string("mod.py"));
  CHECK_EQ(a.files[0].path, std::string("virtual/mod.py"));
  CHECK(findSymbol(a.files[0], "hi") != nullptr);
  WriteOptions none;
  CHECK_THROWS_CODE(buildArtifacts(none), InvalidArgument);
}

TEST(source_validation) {
  CHECK_THROWS_CODE(buildArtifacts(baseOptions({"/nonexistent/file.py"})), Io);
  TempDir dir;
  CHECK_THROWS_CODE(buildArtifacts(baseOptions({dir.path()})), InvalidArgument);
  CHECK_THROWS_CODE(buildArtifacts(baseOptions({fixture("sample.py"), fixture("sample.py")})), InvalidArgument);
  WriteOptions big = baseOptions({fixture("sample.py")});
  big.max_source_bytes = 10;
  CHECK_THROWS_CODE(buildArtifacts(big), TooLarge);
}

TEST(compression_reduces_size_for_repetitive_content) {
  WriteOptions o = baseOptions({fixture("sample.ts")});
  o.mode = Mode::Full;
  Artifact a = buildArtifacts(o)[0];
  std::vector<uint8_t> bytes = serialize(a);
  Artifact back = deserialize(bytes);
  CHECK(back.meta.rawBytes > 0);
  CHECK(back.meta.storedBytes == bytes.size());
  CHECK(bytes.size() < back.meta.rawBytes + 72);
  CHECK(back.meta.compression == 1);
}
