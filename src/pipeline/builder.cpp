#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "context.h"
#include "mfdp/mfdp.h"
#include "parsed.h"
#include "resolve.h"

namespace mfdp {

namespace {

struct Resolved {
  bool imports = true;
  ImportMode importMode = ImportMode::Relevant;
  bool types = true;
  bool deps = true;
  uint32_t depDepth = 1;
  bool lines = true;
  bool hash = true;
  bool allSymbols = true;
  std::vector<std::string> keywords;
};

bool pick(const std::optional<bool>& a, const std::optional<bool>& b, const char* nameA,
          const char* nameB, bool fallback, bool& explicitlySet) {
  explicitlySet = a.has_value() || b.has_value();
  if (a && b && *a != *b) {
    throw Error(ErrorCode::Conflict, std::string("conflicting values for '") + nameA + "' and '" +
                                         nameB + "'");
  }
  if (a) return *a;
  if (b) return *b;
  return fallback;
}

std::string trim(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

Resolved resolveOptions(const WriteOptions& o) {
  Resolved r;
  bool incSet = false;
  bool typesSet = false;
  bool inc = pick(o.inc, o.include, "inc", "include", true, incSet);
  bool inctyps = pick(o.inctyps, o.include_types, "inctyps", "include_types", true, typesSet);
  if (o.legacy_inctyps) {
    r.types = true;
    r.importMode = o.import_mode ? *o.import_mode
                                 : (inctyps ? ImportMode::All : ImportMode::Relevant);
    if (!inc && !o.import_mode) r.importMode = ImportMode::None;
  } else {
    r.types = inctyps;
    if (o.import_mode) {
      r.importMode = *o.import_mode;
      if (incSet && !inc && r.importMode != ImportMode::None) {
        throw Error(ErrorCode::Conflict, "inc/include is false but import_mode is not 'none'");
      }
      if (incSet && inc && r.importMode == ImportMode::None) {
        throw Error(ErrorCode::Conflict, "inc/include is true but import_mode is 'none'");
      }
    } else {
      r.importMode = inc ? ImportMode::Relevant : ImportMode::None;
    }
  }
  r.imports = r.importMode != ImportMode::None;
  r.deps = o.deps;
  if (o.dep_depth < 1 || o.dep_depth > 8) {
    throw Error(ErrorCode::InvalidArgument, "dep_depth must be between 1 and 8");
  }
  r.depDepth = o.dep_depth;
  r.lines = o.lines;
  r.hash = o.hash;
  std::string k = trim(o.kwrds);
  if (k.empty()) throw Error(ErrorCode::InvalidArgument, "kwrds must not be empty; use '*' for all symbols");
  if (k == "*") {
    r.allSymbols = true;
  } else {
    r.allSymbols = false;
    std::string cur;
    std::set<std::string> seen;
    std::stringstream ss(k);
    while (std::getline(ss, cur, ',')) {
      std::string t = trim(cur);
      if (t.empty()) continue;
      if (t == "*") throw Error(ErrorCode::InvalidArgument, "'*' cannot be combined with symbol names");
      if (seen.insert(t).second) r.keywords.push_back(t);
    }
    if (r.keywords.empty()) throw Error(ErrorCode::InvalidArgument, "kwrds contains no symbol names");
  }
  return r;
}

struct Input {
  std::string path;
  std::string filename;
  std::string content;
};

std::vector<Input> loadInputs(const WriteOptions& o) {
  std::vector<Input> inputs;
  std::set<std::string> seen;
  for (const auto& p : o.src) {
    if (p.empty()) throw Error(ErrorCode::InvalidArgument, "empty source path");
    if (p.find('\0') != std::string::npos) throw Error(ErrorCode::InvalidArgument, "source path contains NUL");
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) throw Error(ErrorCode::Io, "cannot access source file", p);
    if (!S_ISREG(st.st_mode)) throw Error(ErrorCode::InvalidArgument, "source is not a regular file", p);
    if (static_cast<uint64_t>(st.st_size) > o.max_source_bytes) {
      throw Error(ErrorCode::TooLarge, "source file exceeds max_source_bytes", p);
    }
    std::string key = std::filesystem::absolute(p).lexically_normal().generic_string();
    if (!seen.insert(key).second) throw Error(ErrorCode::InvalidArgument, "duplicate source file", p);
    std::ifstream in(p, std::ios::binary);
    if (!in) throw Error(ErrorCode::Io, "cannot open source file", p);
    std::ostringstream buf;
    buf << in.rdbuf();
    Input i;
    i.path = std::filesystem::path(p).lexically_normal().generic_string();
    i.filename = baseName(p);
    i.content = buf.str();
    inputs.push_back(std::move(i));
  }
  for (const auto& m : o.memory) {
    if (m.filename.empty()) throw Error(ErrorCode::InvalidArgument, "in-memory source requires a filename");
    if (m.content.size() > o.max_source_bytes) {
      throw Error(ErrorCode::TooLarge, "in-memory source exceeds max_source_bytes", m.filename);
    }
    std::string key = std::filesystem::absolute(m.filename).lexically_normal().generic_string();
    if (!seen.insert(key).second) throw Error(ErrorCode::InvalidArgument, "duplicate source file", m.filename);
    Input i;
    i.path = std::filesystem::path(m.filename).lexically_normal().generic_string();
    i.filename = baseName(m.filename);
    i.content = m.content;
    inputs.push_back(std::move(i));
  }
  if (inputs.empty()) throw Error(ErrorCode::InvalidArgument, "src is required");
  return inputs;
}

struct GroupState {
  std::vector<const ParsedFile*> files;
  std::vector<std::vector<Dependency>> deps;
  std::vector<std::vector<char>> selected;
  std::vector<std::vector<char>> stored;
  std::vector<std::vector<char>> expanded;
  std::vector<std::vector<char>> contextOnly;
};

Artifact buildGroup(const std::vector<const ParsedFile*>& files, const WriteOptions& o,
                    const Resolved& ro, std::set<std::string>& matchedKeywords,
                    std::vector<Warning>& groupWarnings, bool& anySelected) {
  GroupState g;
  g.files = files;
  g.deps = resolveDependencies(files);
  size_t nf = files.size();
  g.selected.resize(nf);
  g.stored.resize(nf);
  g.expanded.resize(nf);
  g.contextOnly.resize(nf);
  std::set<std::string> keywordSet(ro.keywords.begin(), ro.keywords.end());
  std::vector<std::vector<int>> dist(nf);

  struct Ref { int f; int s; };
  std::vector<Ref> frontier;
  std::set<std::string> excludedTypeKeywords;
  for (size_t f = 0; f < nf; ++f) {
    size_t ns = files[f]->symbols.size();
    g.selected[f].assign(ns, 0);
    g.stored[f].assign(ns, 0);
    g.expanded[f].assign(ns, 0);
    g.contextOnly[f].assign(ns, 0);
    dist[f].assign(ns, 0);
    for (size_t s = 0; s < ns; ++s) {
      const Symbol& sym = files[f]->symbols[s].sym;
      bool match = ro.allSymbols;
      if (!ro.allSymbols) {
        bool byName = keywordSet.count(sym.name) > 0;
        bool byQual = keywordSet.count(sym.qualified) > 0;
        match = byName || byQual;
        if (match) {
          if (byName) matchedKeywords.insert(sym.name);
          if (byQual) matchedKeywords.insert(sym.qualified);
        }
      }
      if (!match) continue;
      if (isTypeKind(sym.kind) && !ro.types) {
        if (!ro.allSymbols) excludedTypeKeywords.insert(sym.qualified);
        continue;
      }
      g.selected[f][s] = 1;
      g.stored[f][s] = 1;
      frontier.push_back({static_cast<int>(f), static_cast<int>(s)});
      anySelected = true;
    }
  }
  for (const auto& k : excludedTypeKeywords) {
    groupWarnings.push_back({"type_excluded", "symbol '" + k + "' is a type and inctyps/include_types is false"});
  }

  if (ro.deps) {
    for (uint32_t d = 1; d <= ro.depDepth && !frontier.empty(); ++d) {
      std::vector<Ref> next;
      for (const Ref& r : frontier) {
        g.expanded[r.f][r.s] = 1;
        for (const Dependency& dep : g.deps[r.f]) {
          if (dep.from != static_cast<uint32_t>(r.s)) continue;
          if (dep.targetSymbol < 0) continue;
          if (dep.status != DepStatus::SameFile && dep.status != DepStatus::CrossFile) continue;
          int tf = dep.targetFile;
          int ts = dep.targetSymbol;
          const Symbol& target = files[tf]->symbols[ts].sym;
          if (isTypeKind(target.kind) && !ro.types) continue;
          if (!g.stored[tf][ts]) {
            g.stored[tf][ts] = 1;
            g.contextOnly[tf][ts] = 1;
            dist[tf][ts] = static_cast<int>(d);
            next.push_back({tf, ts});
          }
        }
      }
      frontier = std::move(next);
    }
  }

  for (size_t f = 0; f < nf; ++f) {
    const auto& syms = files[f]->symbols;
    for (size_t s = 0; s < syms.size(); ++s) {
      if (!(g.selected[f][s] && isContainerKind(syms[s].sym.kind))) continue;
      for (size_t c = 0; c < syms.size(); ++c) {
        if (syms[c].sym.parentPlus1 == s + 1 && !g.stored[f][c]) {
          if (isTypeKind(syms[c].sym.kind) && !ro.types) continue;
          g.stored[f][c] = 1;
          g.contextOnly[f][c] = 1;
        }
      }
    }
  }
  bool stubWarned = false;
  for (size_t f = 0; f < nf; ++f) {
    const auto& syms = files[f]->symbols;
    for (size_t s = 0; s < syms.size(); ++s) {
      if (!g.stored[f][s]) continue;
      int p = static_cast<int>(syms[s].sym.parentPlus1) - 1;
      while (p >= 0) {
        if (!g.stored[f][p]) {
          g.stored[f][p] = 1;
          g.contextOnly[f][p] = 1;
          if (isTypeKind(syms[p].sym.kind) && !ro.types && !stubWarned) {
            stubWarned = true;
            groupWarnings.push_back({"type_ancestor_stub",
                                     "types are excluded but enclosing type declarations are kept as "
                                     "signature-only stubs to preserve parent-child relationships"});
          }
        }
        p = static_cast<int>(syms[p].sym.parentPlus1) - 1;
      }
    }
  }

  Mode requested = o.mode;
  Mode resolvedMode = requested;
  if (requested == Mode::Auto) resolvedMode = ro.allSymbols ? Mode::Symbols : Mode::Full;

  Artifact art;
  art.meta.requestedMode = requested;
  art.meta.resolvedMode = resolvedMode;
  art.meta.importMode = ro.importMode;
  art.meta.combined = o.combined;
  art.meta.budget = o.budget;
  art.meta.depDepth = ro.depDepth;
  art.meta.kwrds = ro.allSymbols ? "*" : trim(o.kwrds);
  uint32_t flags = 0;
  if (ro.lines) flags |= kFlagLines;
  if (ro.imports) flags |= kFlagImports;
  if (ro.types) flags |= kFlagTypes;
  if (ro.deps) flags |= kFlagDeps;
  if (o.combined) flags |= kFlagCombined;
  art.meta.flags = flags;

  for (size_t f = 0; f < nf; ++f) {
    const ParsedFile& pf = *files[f];
    FileRecord rec;
    rec.path = pf.path;
    rec.filename = pf.filename;
    rec.language = pf.language;
    rec.sourceSize = pf.content.size();
    rec.hasHash = ro.hash;
    if (ro.hash) rec.hash = pf.hash;
    rec.totalSymbols = static_cast<uint32_t>(pf.symbols.size());
    rec.totalImports = static_cast<uint32_t>(pf.imports.size());
    rec.exports = pf.exports;

    std::vector<int> remap(pf.symbols.size(), -1);
    std::vector<size_t> order;
    for (size_t s = 0; s < pf.symbols.size(); ++s) {
      if (g.stored[f][s]) {
        remap[s] = static_cast<int>(order.size());
        order.push_back(s);
      }
    }
    std::set<std::string> used;
    for (size_t s : order) {
      const RawSymbol& rs = pf.symbols[s];
      for (const auto& c : rs.calls) used.insert(c.substr(0, c.find('.')));
      for (const auto& r : rs.refs) used.insert(r.substr(0, r.find('.')));
      for (const auto& b : rs.sym.bases) used.insert(b.substr(0, b.find('.')));
    }
    std::vector<char> fragmentHolder(order.size(), 0);
    for (size_t i = 0; i < order.size(); ++i) {
      size_t s = order[i];
      Symbol sym = pf.symbols[s].sym;
      sym.selected = g.selected[f][s] != 0;
      sym.context = !sym.selected;
      sym.hasFragment = false;
      sym.coveredByAncestor = false;
      sym.fragment.clear();
      if (sym.parentPlus1 > 0) {
        int np = remap[sym.parentPlus1 - 1];
        sym.parentPlus1 = np < 0 ? 0 : static_cast<uint32_t>(np) + 1;
      }
      if (sym.selected && resolvedMode == Mode::Full && !pf.symbols[s].sym.fragment.empty()) {
        bool covered = false;
        uint32_t p = sym.parentPlus1;
        while (p > 0) {
          if (fragmentHolder[p - 1]) {
            covered = true;
            break;
          }
          p = rec.symbols[p - 1].parentPlus1;
        }
        if (covered) {
          sym.coveredByAncestor = true;
        } else {
          sym.hasFragment = true;
          sym.fragment = pf.symbols[s].sym.fragment;
          fragmentHolder[i] = 1;
        }
      }
      if (!ro.lines) {
        sym.startLine = 0;
        sym.endLine = 0;
      }
      rec.symbols.push_back(std::move(sym));
    }
    for (const Dependency& dep : g.deps[f]) {
      if (!g.expanded[f][dep.from]) continue;
      int from = remap[dep.from];
      if (from < 0) continue;
      Dependency out = dep;
      out.from = static_cast<uint32_t>(from);
      if (dep.targetSymbol >= 0) {
        int tf = dep.targetFile;
        if (tf < 0 || !g.stored[tf][dep.targetSymbol]) continue;
      }
      rec.deps.push_back(out);
    }
    for (const Import& imp : pf.imports) {
      bool include = false;
      if (ro.importMode == ImportMode::All) {
        include = true;
      } else if (ro.importMode == ImportMode::Relevant) {
        include = imp.star;
        for (const auto& b : imp.bindings) {
          if (used.count(b.local)) include = true;
        }
      }
      if (!include) continue;
      Import out = imp;
      if (!ro.lines) {
        out.line = 0;
        out.endLine = 0;
      }
      rec.imports.push_back(std::move(out));
    }
    art.files.push_back(std::move(rec));
  }

  std::vector<std::vector<int>> fileRemap(nf);
  for (size_t f = 0; f < nf; ++f) {
    fileRemap[f].assign(files[f]->symbols.size(), -1);
    int idx = 0;
    for (size_t s = 0; s < files[f]->symbols.size(); ++s) {
      if (g.stored[f][s]) fileRemap[f][s] = idx++;
    }
  }
  for (size_t f = 0; f < nf; ++f) {
    for (auto& dep : art.files[f].deps) {
      if (dep.targetSymbol >= 0) dep.targetSymbol = fileRemap[dep.targetFile][dep.targetSymbol];
    }
  }
  return art;
}

}

std::vector<Artifact> buildArtifacts(const WriteOptions& options, std::vector<Warning>* warnings) {
  Resolved ro = resolveOptions(options);
  std::vector<Input> inputs = loadInputs(options);

  std::vector<ParsedFile> parsed;
  parsed.reserve(inputs.size());
  for (const auto& in : inputs) {
    std::string lang = resolveLanguage(in.path, options.lang);
    parsed.push_back(parseSource(in.path, in.filename, in.content, lang));
  }

  std::vector<std::vector<const ParsedFile*>> groups;
  if (options.combined) {
    groups.emplace_back();
    for (const auto& p : parsed) groups.back().push_back(&p);
  } else {
    for (const auto& p : parsed) groups.push_back({&p});
  }

  std::set<std::string> matched;
  std::vector<Artifact> artifacts;
  std::vector<std::vector<Warning>> perGroupWarnings;
  bool anySelected = false;
  for (const auto& group : groups) {
    std::vector<Warning> gw;
    bool groupSelected = false;
    Artifact art = buildGroup(group, options, ro, matched, gw, groupSelected);
    if (!groupSelected) {
      gw.push_back({"no_symbols_selected", "no symbols were selected in this artifact"});
    }
    anySelected = anySelected || groupSelected;
    artifacts.push_back(std::move(art));
    perGroupWarnings.push_back(std::move(gw));
  }
  if (!anySelected) {
    throw Error(ErrorCode::NoMatch, "no parsed symbols matched kwrds '" + options.kwrds + "'");
  }

  std::vector<Warning> global;
  if (!ro.allSymbols) {
    for (const auto& k : ro.keywords) {
      if (!matched.count(k)) {
        global.push_back({"symbol_not_found", "no parsed symbol named '" + k + "' was found"});
      }
    }
  }

  for (size_t i = 0; i < artifacts.size(); ++i) {
    Artifact& a = artifacts[i];
    a.meta.warnings = global;
    for (auto& w : perGroupWarnings[i]) a.meta.warnings.push_back(w);
    if (options.mode == Mode::Auto && a.meta.resolvedMode == Mode::Full && options.budget > 0) {
      ExtractOptions eo;
      eo.kwrds = "*";
      eo.expand = Expand::Always;
      eo.budget = 0;
      ExtractResult er = renderContext(a, eo);
      if (er.tokens > options.budget) {
        for (auto& file : a.files) {
          for (auto& sym : file.symbols) {
            sym.hasFragment = false;
            sym.coveredByAncestor = false;
            sym.fragment.clear();
          }
        }
        a.meta.resolvedMode = Mode::Symbols;
        a.meta.warnings.push_back({"auto_mode_downgraded",
                                   "full context (~" + std::to_string(er.tokens) +
                                       " tokens) exceeds budget " + std::to_string(options.budget) +
                                       "; stored symbols mode instead"});
      }
    }
  }
  if (warnings) {
    for (const auto& w : global) warnings->push_back(w);
    for (size_t i = 0; i < artifacts.size(); ++i) {
      for (const auto& w : perGroupWarnings[i]) warnings->push_back(w);
    }
  }
  return artifacts;
}

}
