#include "resolve.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace mfdp {

namespace {

struct ResolvedBinding {
  std::string local;
  std::string imported;
  std::string module;
  uint32_t depth = 1;
  int target = -1;
};

struct Target {
  DepStatus status = DepStatus::Unresolved;
  int file = -1;
  int sym = -1;
  std::string module;
};

struct Res {
  bool handled = false;
  bool hasTarget = false;
  bool refOnly = false;
  Target t;
};

struct FileCtx {
  const ParsedFile* pf = nullptr;
  std::string norm;
  std::map<std::pair<int, std::string>, int> index;
  std::map<std::string, int> nsLevel;
  std::map<std::string, ResolvedBinding> bindings;
};

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

bool isFunctionLike(SymbolKind k) {
  return k == SymbolKind::Function || k == SymbolKind::Method || k == SymbolKind::Constructor;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class Resolver {
 public:
  explicit Resolver(const std::vector<const ParsedFile*>& group) {
    ctx_.resize(group.size());
    for (size_t i = 0; i < group.size(); ++i) {
      FileCtx& c = ctx_[i];
      c.pf = group[i];
      std::error_code ec;
      std::filesystem::path abs = std::filesystem::absolute(group[i]->path, ec);
      c.norm = (ec ? std::filesystem::path(group[i]->path) : abs).lexically_normal().generic_string();
      normToIndex_[c.norm] = static_cast<int>(i);
      const auto& syms = c.pf->symbols;
      for (size_t s = 0; s < syms.size(); ++s) {
        auto key = std::make_pair(static_cast<int>(syms[s].sym.parentPlus1) - 1, syms[s].sym.name);
        if (c.index.find(key) == c.index.end()) c.index[key] = static_cast<int>(s);
        bool namespaceLevel = true;
        int p = static_cast<int>(syms[s].sym.parentPlus1) - 1;
        while (p >= 0) {
          if (syms[p].sym.kind != SymbolKind::Namespace) {
            namespaceLevel = false;
            break;
          }
          p = static_cast<int>(syms[p].sym.parentPlus1) - 1;
        }
        if (namespaceLevel && c.nsLevel.find(syms[s].sym.name) == c.nsLevel.end()) {
          c.nsLevel[syms[s].sym.name] = static_cast<int>(s);
        }
      }
    }
    for (size_t i = 0; i < ctx_.size(); ++i) buildBindings(static_cast<int>(i));
    buildFallbacks();
  }

  std::vector<std::vector<Dependency>> run() {
    std::vector<std::vector<Dependency>> result(ctx_.size());
    for (size_t f = 0; f < ctx_.size(); ++f) {
      const auto& syms = ctx_[f].pf->symbols;
      for (size_t s = 0; s < syms.size(); ++s) {
        std::set<std::string> seen;
        std::set<std::string> coveredRoots;
        auto handle = [&](const std::string& name, DepKind kind) {
          Res r = resolve(static_cast<int>(f), static_cast<int>(s), name);
          if (r.handled) coveredRoots.insert(name.substr(0, name.find('.')));
          if (!r.hasTarget) return;
          if (r.t.file == static_cast<int>(f) && r.t.sym == static_cast<int>(s)) return;
          DepKind k = kind;
          if (r.refOnly && k == DepKind::Call) k = DepKind::Ref;
          std::string key = std::to_string(static_cast<int>(k)) + "|" + name;
          if (!seen.insert(key).second) return;
          Dependency d;
          d.from = static_cast<uint32_t>(s);
          d.kind = k;
          d.status = r.t.status;
          d.name = name;
          d.module = r.t.module;
          d.targetFile = r.t.status == DepStatus::External ? -1 : r.t.file;
          d.targetSymbol = r.t.sym;
          if (d.status == DepStatus::SameFile) d.targetFile = static_cast<int>(f);
          result[f].push_back(std::move(d));
        };
        for (const auto& b : syms[s].sym.bases) handle(b, DepKind::Base);
        for (const auto& c : syms[s].calls) handle(c, DepKind::Call);
        for (const auto& r : syms[s].refs) {
          if (coveredRoots.count(r.substr(0, r.find('.')))) continue;
          handle(r, DepKind::Ref);
        }
      }
    }
    return result;
  }

 private:
  bool isPython(int f) const { return ctx_[f].pf->language == "python"; }

  int lookupNorm(const std::string& path) const {
    auto it = normToIndex_.find(std::filesystem::path(path).lexically_normal().generic_string());
    return it == normToIndex_.end() ? -1 : it->second;
  }

  int resolveJsModule(int importer, const std::string& module) const {
    if (module.empty() || module[0] != '.') return -1;
    std::filesystem::path base = std::filesystem::path(ctx_[importer].norm).parent_path() / module;
    std::string b = base.lexically_normal().generic_string();
    std::vector<std::string> candidates;
    candidates.push_back(b);
    static const char* exts[] = {".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs", ".mts", ".cts", ".d.ts"};
    std::string stem = b;
    for (const char* e : {".js", ".jsx", ".mjs", ".cjs"}) {
      std::string ext = e;
      if (endsWith(b, ext)) {
        stem = b.substr(0, b.size() - ext.size());
        if (ext == ".js") {
          candidates.push_back(stem + ".ts");
          candidates.push_back(stem + ".tsx");
        } else if (ext == ".mjs") {
          candidates.push_back(stem + ".mts");
        } else if (ext == ".cjs") {
          candidates.push_back(stem + ".cts");
        } else if (ext == ".jsx") {
          candidates.push_back(stem + ".tsx");
        }
        break;
      }
    }
    for (const char* e : exts) candidates.push_back(b + e);
    for (const char* e : exts) candidates.push_back(b + "/index" + e);
    for (const auto& c : candidates) {
      auto it = normToIndex_.find(c);
      if (it != normToIndex_.end()) return it->second;
    }
    return -1;
  }

  int resolvePythonModule(int importer, const std::string& module) const {
    size_t level = 0;
    while (level < module.size() && module[level] == '.') ++level;
    std::string rest = module.substr(level);
    std::string rel = rest;
    std::replace(rel.begin(), rel.end(), '.', '/');
    if (level > 0) {
      std::filesystem::path dir = std::filesystem::path(ctx_[importer].norm).parent_path();
      for (size_t i = 1; i < level; ++i) dir = dir.parent_path();
      std::vector<std::string> candidates;
      if (rel.empty()) {
        candidates.push_back((dir / "__init__.py").lexically_normal().generic_string());
      } else {
        candidates.push_back((dir / (rel + ".py")).lexically_normal().generic_string());
        candidates.push_back((dir / rel / "__init__.py").lexically_normal().generic_string());
      }
      for (const auto& c : candidates) {
        auto it = normToIndex_.find(c);
        if (it != normToIndex_.end()) return it->second;
      }
      return -1;
    }
    if (rel.empty()) return -1;
    std::vector<int> matches;
    std::string a = "/" + rel + ".py";
    std::string b = "/" + rel + "/__init__.py";
    for (size_t i = 0; i < ctx_.size(); ++i) {
      const std::string& n = ctx_[i].norm;
      if (endsWith(n, a) || endsWith(n, b)) matches.push_back(static_cast<int>(i));
    }
    if (matches.size() == 1) return matches[0];
    if (matches.size() > 1) return -2;
    return -1;
  }

  int resolveCModule(int importer, const std::string& module) const {
    if (module.empty()) return -1;
    std::filesystem::path dir = std::filesystem::path(ctx_[importer].norm).parent_path();
    auto direct = normToIndex_.find((dir / module).lexically_normal().generic_string());
    if (direct != normToIndex_.end()) return direct->second;
    if (module[0] == '.') return -1;
    std::string suffix = "/" + std::filesystem::path(module).lexically_normal().generic_string();
    int found = -1;
    for (size_t i = 0; i < ctx_.size(); ++i) {
      if (endsWith(ctx_[i].norm, suffix)) {
        if (found >= 0) return -1;
        found = static_cast<int>(i);
      }
    }
    return found;
  }

  int resolveJavaModule(const std::string& module) const {
    std::string path = module;
    std::replace(path.begin(), path.end(), '.', '/');
    while (!path.empty()) {
      std::string suffix = "/" + path + ".java";
      int found = -1;
      bool ambiguous = false;
      for (size_t i = 0; i < ctx_.size(); ++i) {
        if (endsWith(ctx_[i].norm, suffix) || ctx_[i].norm == path + ".java") {
          if (found >= 0) ambiguous = true;
          found = static_cast<int>(i);
        }
      }
      if (found >= 0 && !ambiguous) return found;
      size_t slash = path.find_last_of('/');
      if (slash == std::string::npos) break;
      path = path.substr(0, slash);
    }
    return -1;
  }

  int resolveRustModule(int importer, const std::string& module) const {
    std::vector<std::string> segs;
    size_t pos = 0;
    while (pos <= module.size()) {
      size_t next = module.find("::", pos);
      if (next == std::string::npos) {
        segs.push_back(module.substr(pos));
        break;
      }
      segs.push_back(module.substr(pos, next - pos));
      pos = next + 2;
    }
    if (segs.empty() || segs[0].empty()) return -1;
    size_t i = 0;
    int ups = 0;
    bool relative = false;
    if (segs[0] == "crate") {
      i = 1;
    } else if (segs[0] == "self") {
      relative = true;
      i = 1;
    } else if (segs[0] == "super") {
      relative = true;
      while (i < segs.size() && segs[i] == "super") {
        ++ups;
        ++i;
      }
    }
    std::string joined;
    for (size_t k = i; k < segs.size(); ++k) {
      if (!joined.empty()) joined += "/";
      joined += segs[k];
    }
    if (joined.empty()) {
      if (relative && ups == 0) return importer;
      if (relative) return -1;
      int found = -1;
      for (size_t k = 0; k < ctx_.size(); ++k) {
        std::string name = std::filesystem::path(ctx_[k].norm).filename().string();
        if (name == "lib.rs" || name == "main.rs") {
          if (found >= 0) return -1;
          found = static_cast<int>(k);
        }
      }
      return found;
    }
    if (relative) {
      std::filesystem::path self(ctx_[importer].norm);
      std::string name = self.filename().string();
      std::filesystem::path base = self.parent_path();
      if (name != "mod.rs" && name != "lib.rs" && name != "main.rs") base /= self.stem();
      for (int u = 0; u < ups; ++u) base = base.parent_path();
      for (const std::string& cand : {(base / (joined + ".rs")).lexically_normal().generic_string(),
                                      (base / joined / "mod.rs").lexically_normal().generic_string()}) {
        auto it = normToIndex_.find(cand);
        if (it != normToIndex_.end()) return it->second;
      }
      return -1;
    }
    std::string a = "/" + joined + ".rs";
    std::string b = "/" + joined + "/mod.rs";
    int found = -1;
    for (size_t k = 0; k < ctx_.size(); ++k) {
      if (endsWith(ctx_[k].norm, a) || endsWith(ctx_[k].norm, b)) {
        if (found >= 0) return -2;
        found = static_cast<int>(k);
      }
    }
    return found;
  }

  int resolveModule(int importer, const std::string& module) const {
    const std::string& lang = ctx_[importer].pf->language;
    if (lang == "python") return resolvePythonModule(importer, module);
    if (lang == "c" || lang == "cpp") return resolveCModule(importer, module);
    if (lang == "java") return resolveJavaModule(module);
    if (lang == "rust") return resolveRustModule(importer, module);
    if (lang == "go" || lang == "csharp") return -1;
    return resolveJsModule(importer, module);
  }

  void buildFallbacks() {
    fallback_.assign(ctx_.size(), std::vector<int>());
    std::vector<std::vector<int>> direct(ctx_.size());
    for (size_t f = 0; f < ctx_.size(); ++f) {
      for (const auto& imp : ctx_[f].pf->imports) {
        if (!imp.star) continue;
        int t = resolveModule(static_cast<int>(f), imp.module);
        if (t >= 0 && t != static_cast<int>(f)) direct[f].push_back(t);
      }
    }
    for (size_t f = 0; f < ctx_.size(); ++f) {
      std::vector<char> seen(ctx_.size(), 0);
      seen[f] = 1;
      std::vector<int> queue = direct[f];
      for (size_t q = 0; q < queue.size(); ++q) {
        int t = queue[q];
        if (seen[t]) continue;
        seen[t] = 1;
        fallback_[f].push_back(t);
        const std::string& lang = ctx_[t].pf->language;
        if (lang == "c" || lang == "cpp") {
          for (int next : direct[t]) queue.push_back(next);
        }
      }
      if (ctx_[f].pf->language == "java") {
        std::filesystem::path dir = std::filesystem::path(ctx_[f].norm).parent_path();
        for (size_t o = 0; o < ctx_.size(); ++o) {
          if (o == f || ctx_[o].pf->language != "java" || seen[o]) continue;
          if (std::filesystem::path(ctx_[o].norm).parent_path() == dir) {
            seen[o] = 1;
            fallback_[f].push_back(static_cast<int>(o));
          }
        }
        for (const auto& imp : ctx_[f].pf->imports) {
          if (!imp.star || imp.module.empty()) continue;
          std::string pkg = imp.module;
          std::replace(pkg.begin(), pkg.end(), '.', '/');
          for (size_t o = 0; o < ctx_.size(); ++o) {
            if (o == f || ctx_[o].pf->language != "java" || seen[o]) continue;
            std::string odir = std::filesystem::path(ctx_[o].norm).parent_path().generic_string();
            if (endsWith(odir, "/" + pkg) || odir == pkg) {
              seen[o] = 1;
              fallback_[f].push_back(static_cast<int>(o));
            }
          }
        }
      }
      if (ctx_[f].pf->language == "csharp") {
        for (size_t o = 0; o < ctx_.size(); ++o) {
          if (o == f || ctx_[o].pf->language != "csharp" || seen[o]) continue;
          seen[o] = 1;
          fallback_[f].push_back(static_cast<int>(o));
        }
      }
      if (ctx_[f].pf->language == "go") {
        std::filesystem::path dir = std::filesystem::path(ctx_[f].norm).parent_path();
        for (size_t o = 0; o < ctx_.size(); ++o) {
          if (o == f || ctx_[o].pf->language != "go" || seen[o]) continue;
          if (std::filesystem::path(ctx_[o].norm).parent_path() == dir) {
            seen[o] = 1;
            fallback_[f].push_back(static_cast<int>(o));
          }
        }
      }
    }
  }

  void buildBindings(int f) {
    FileCtx& fc = ctx_[f];
    for (const auto& imp : fc.pf->imports) {
      for (const auto& b : imp.bindings) {
        ResolvedBinding rb;
        rb.local = b.local;
        rb.imported = b.imported;
        rb.module = b.module.empty() ? imp.module : b.module;
        rb.depth = b.depth;
        rb.target = resolveModule(f, rb.module);
        const bool rustLang = fc.pf->language == "rust";
        if ((isPython(f) || rustLang) && rb.imported != "*" && !imp.text.empty()) {
          bool symbolFound = rb.target >= 0 && topLevel(rb.target, rb.imported) >= 0;
          if (!symbolFound) {
            std::string sub = rb.module;
            if (rustLang) {
              if (!sub.empty()) sub += "::";
            } else if (sub.empty() || sub.back() != '.') {
              sub += ".";
            }
            sub += rb.imported;
            int subTarget = resolveModule(f, sub);
            if (subTarget >= 0) {
              rb.target = subTarget;
              rb.module = sub;
              rb.imported = "*";
              rb.depth = 1;
            }
          }
        }
        fc.bindings[rb.local] = rb;
      }
    }
  }

  int topLevel(int f, const std::string& name) const {
    auto it = ctx_[f].index.find(std::make_pair(-1, name));
    if (it != ctx_[f].index.end()) return it->second;
    auto ns = ctx_[f].nsLevel.find(name);
    return ns == ctx_[f].nsLevel.end() ? -1 : ns->second;
  }

  int defaultSymbol(int f) const {
    const auto& syms = ctx_[f].pf->symbols;
    for (size_t i = 0; i < syms.size(); ++i) {
      if (syms[i].sym.parentPlus1 == 0 && syms[i].defaultExport) return static_cast<int>(i);
    }
    return -1;
  }

  SymbolKind kindOf(int f, int s) const { return ctx_[f].pf->symbols[s].sym.kind; }

  int scopeLookup(int f, int s, const std::string& name) const {
    const FileCtx& fc = ctx_[f];
    const auto& syms = fc.pf->symbols;
    std::vector<int> scopes;
    if (isFunctionLike(syms[s].sym.kind) || syms[s].sym.kind == SymbolKind::Namespace) scopes.push_back(s);
    int p = static_cast<int>(syms[s].sym.parentPlus1) - 1;
    while (p >= 0) {
      if (isFunctionLike(syms[p].sym.kind) || syms[p].sym.kind == SymbolKind::Namespace) scopes.push_back(p);
      p = static_cast<int>(syms[p].sym.parentPlus1) - 1;
    }
    scopes.push_back(-1);
    for (int scope : scopes) {
      auto it = fc.index.find(std::make_pair(scope, name));
      if (it != fc.index.end() && it->second != s) return it->second;
    }
    return -1;
  }

  int enclosingClass(int f, int s) const {
    const auto& syms = ctx_[f].pf->symbols;
    int cur = s;
    while (cur >= 0) {
      SymbolKind k = syms[cur].sym.kind;
      if (k == SymbolKind::Class || k == SymbolKind::Interface) return cur;
      cur = static_cast<int>(syms[cur].sym.parentPlus1) - 1;
    }
    return -1;
  }

  bool findMember(int origin, int f, int cls, const std::string& name, int depth, Target& out) {
    auto it = ctx_[f].index.find(std::make_pair(cls, name));
    if (it != ctx_[f].index.end()) {
      out.file = f;
      out.sym = it->second;
      out.status = f == origin ? DepStatus::SameFile : DepStatus::CrossFile;
      return true;
    }
    if (depth >= 6) return false;
    const auto& bases = ctx_[f].pf->symbols[cls].sym.bases;
    for (const auto& base : bases) {
      Res br = resolve(f, cls, base);
      if (br.hasTarget && br.t.sym >= 0 &&
          (br.t.status == DepStatus::SameFile || br.t.status == DepStatus::CrossFile) &&
          isContainerKind(kindOf(br.t.file, br.t.sym))) {
        if (findMember(origin, br.t.file, br.t.sym, name, depth + 1, out)) return true;
      }
    }
    return false;
  }

  void descend(int origin, Res& r, const std::vector<std::string>& rest) {
    size_t i = 0;
    while (i < rest.size()) {
      if (r.t.sym < 0 || !isContainerKind(kindOf(r.t.file, r.t.sym))) break;
      Target m;
      if (!findMember(origin, r.t.file, r.t.sym, rest[i], 0, m)) break;
      r.t = m;
      ++i;
    }
    if (i < rest.size()) r.refOnly = true;
  }

  Res resolve(int f, int s, const std::string& name) {
    Res r;
    std::vector<std::string> segs = split(name, '.');
    const std::string& root = segs[0];
    FileCtx& fc = ctx_[f];

    if (root == "this") {
      r.handled = true;
      if (segs.size() < 2) return r;
      int cls = enclosingClass(f, s);
      if (cls < 0) return r;
      Target t;
      if (findMember(f, f, cls, segs[1], 0, t)) {
        r.hasTarget = true;
        r.t = t;
      }
      return r;
    }
    if (root == "super") {
      r.handled = true;
      return r;
    }

    int local = scopeLookup(f, s, root);
    if (local >= 0) {
      r.handled = true;
      r.hasTarget = true;
      r.t.status = DepStatus::SameFile;
      r.t.file = f;
      r.t.sym = local;
      if (segs.size() > 1) descend(f, r, std::vector<std::string>(segs.begin() + 1, segs.end()));
      return r;
    }

    auto bit = fc.bindings.find(root);
    if (bit != fc.bindings.end()) {
      const ResolvedBinding& b = bit->second;
      r.handled = true;
      r.hasTarget = true;
      if (b.target == -1) {
        r.t.status = DepStatus::External;
        r.t.module = b.module;
        return r;
      }
      if (b.target == -2) {
        r.t.status = DepStatus::Unresolved;
        r.t.module = b.module;
        return r;
      }
      int tf = b.target;
      r.t.module = b.module;
      r.t.file = tf;
      std::vector<std::string> rest;
      int symIdx = -1;
      if (b.imported == "*") {
        size_t consume = std::min<size_t>(b.depth, segs.size());
        rest.assign(segs.begin() + consume, segs.end());
        if (rest.empty()) {
          r.t.status = DepStatus::CrossFile;
          r.t.sym = -1;
          return r;
        }
        symIdx = topLevel(tf, rest[0]);
        if (symIdx < 0) {
          r.t.status = DepStatus::Unresolved;
          r.t.sym = -1;
          return r;
        }
        rest.erase(rest.begin());
      } else {
        symIdx = b.imported == "default" ? defaultSymbol(tf) : topLevel(tf, b.imported);
        rest.assign(segs.begin() + 1, segs.end());
        if (symIdx < 0) {
          r.t.status = DepStatus::Unresolved;
          r.t.sym = -1;
          return r;
        }
      }
      r.t.status = DepStatus::CrossFile;
      r.t.sym = symIdx;
      r.t.module.clear();
      if (!rest.empty()) descend(f, r, rest);
      return r;
    }

    for (int tf : fallback_[f]) {
      int symIdx = topLevel(tf, root);
      if (symIdx < 0) continue;
      r.handled = true;
      r.hasTarget = true;
      r.t.status = DepStatus::CrossFile;
      r.t.file = tf;
      r.t.sym = symIdx;
      if (segs.size() > 1) descend(f, r, std::vector<std::string>(segs.begin() + 1, segs.end()));
      return r;
    }

    if (segs.size() == 1) {
      r.handled = true;
      r.hasTarget = true;
      r.t.status = DepStatus::Unresolved;
      return r;
    }
    return r;
  }

  std::vector<FileCtx> ctx_;
  std::vector<std::vector<int>> fallback_;
  std::map<std::string, int> normToIndex_;
};

}

std::vector<std::vector<Dependency>> resolveDependencies(const std::vector<const ParsedFile*>& group) {
  Resolver r(group);
  return r.run();
}

}
