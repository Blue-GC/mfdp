#include "context.h"

#include <algorithm>
#include <set>
#include <sstream>

#include "mfdp/mfdp.h"

namespace mfdp {

namespace {

struct Part {
  int file = 0;
  int section = 0;
  int sym = 0;
  int slot = 0;
  int tier = 0;
  std::string text;
  uint64_t tokens = 0;
  bool include = false;
  bool isFragment = false;
};

std::string lineRange(const Symbol& s, bool lines) {
  if (!lines || s.startLine == 0) return std::string();
  std::string out = " [L" + std::to_string(s.startLine);
  if (s.endLine > s.startLine) out += "-" + std::to_string(s.endLine);
  return out + "]";
}

std::string firstParagraph(const std::string& doc, size_t limit) {
  std::string out;
  std::istringstream in(doc);
  std::string line;
  bool started = false;
  while (std::getline(in, line)) {
    size_t b = line.find_first_not_of(" \t");
    if (b == std::string::npos) {
      if (started) break;
      continue;
    }
    started = true;
    if (!out.empty()) out += " ";
    out += line.substr(b);
  }
  if (out.size() > limit) out = out.substr(0, limit - 3) + "...";
  return out;
}

std::string depTarget(const Artifact& a, const FileRecord& f, const Dependency& d) {
  switch (d.status) {
    case DepStatus::SameFile:
      if (d.targetSymbol >= 0) return f.symbols[d.targetSymbol].qualified;
      return "same-file";
    case DepStatus::CrossFile: {
      if (d.targetFile < 0) return "cross-file";
      const FileRecord& tf = a.files[d.targetFile];
      if (d.targetSymbol >= 0) return tf.path + "::" + tf.symbols[d.targetSymbol].qualified;
      return tf.path;
    }
    case DepStatus::External:
      return "external(" + d.module + ")";
    case DepStatus::Unresolved:
      if (d.targetFile >= 0) return "unresolved(in " + a.files[d.targetFile].path + ")";
      return d.module.empty() ? "unresolved" : "unresolved(" + d.module + ")";
  }
  return "unresolved";
}

std::string indentLines(const std::string& text, const std::string& indent) {
  std::string out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) out += indent + line + "\n";
  return out;
}

}

ExtractResult renderContext(const Artifact& a, const ExtractOptions& o) {
  ExtractResult res;
  const bool lines = (a.meta.flags & kFlagLines) != 0;
  uint32_t budget = o.budget ? *o.budget : a.meta.budget;
  res.budget = budget;

  std::string kw = o.kwrds;
  bool all = true;
  std::set<std::string> wanted;
  {
    std::string t;
    size_t b = kw.find_first_not_of(" \t");
    size_t e = kw.find_last_not_of(" \t");
    t = b == std::string::npos ? std::string() : kw.substr(b, e - b + 1);
    if (t.empty()) throw Error(ErrorCode::InvalidArgument, "kwrds must not be empty");
    if (t != "*") {
      all = false;
      std::stringstream ss(t);
      std::string cur;
      while (std::getline(ss, cur, ',')) {
        size_t cb = cur.find_first_not_of(" \t");
        size_t ce = cur.find_last_not_of(" \t");
        if (cb == std::string::npos) continue;
        wanted.insert(cur.substr(cb, ce - cb + 1));
      }
      if (wanted.empty()) throw Error(ErrorCode::InvalidArgument, "kwrds contains no symbol names");
    }
  }

  size_t nf = a.files.size();
  std::vector<std::vector<char>> matched(nf);
  std::vector<std::vector<char>> presented(nf);
  std::set<std::string> found;
  for (size_t f = 0; f < nf; ++f) {
    const auto& syms = a.files[f].symbols;
    matched[f].assign(syms.size(), 0);
    presented[f].assign(syms.size(), 0);
    res.symbolsStored += static_cast<uint32_t>(syms.size());
    for (size_t s = 0; s < syms.size(); ++s) {
      bool m = all || wanted.count(syms[s].name) || wanted.count(syms[s].qualified);
      if (m) {
        matched[f][s] = 1;
        presented[f][s] = 1;
        if (!all) {
          if (wanted.count(syms[s].name)) found.insert(syms[s].name);
          if (wanted.count(syms[s].qualified)) found.insert(syms[s].qualified);
        }
      }
    }
  }
  if (!all) {
    for (const auto& w : wanted) {
      if (!found.count(w)) {
        res.warnings.push_back({"symbol_not_found", "no stored symbol named '" + w + "' in this artifact"});
      }
    }
  }
  for (size_t f = 0; f < nf; ++f) {
    const auto& syms = a.files[f].symbols;
    for (size_t s = 0; s < syms.size(); ++s) {
      if (!matched[f][s]) continue;
      uint32_t p = syms[s].parentPlus1;
      while (p > 0) {
        presented[f][p - 1] = 1;
        p = syms[p - 1].parentPlus1;
      }
      if (!all && isContainerKind(syms[s].kind)) {
        for (size_t c = 0; c < syms.size(); ++c) {
          if (syms[c].parentPlus1 == s + 1) presented[f][c] = 1;
        }
      }
    }
  }
  if (!all && o.deps) {
    for (size_t f = 0; f < nf; ++f) {
      for (const auto& d : a.files[f].deps) {
        if (!matched[f][d.from] || d.targetSymbol < 0 || d.targetFile < 0) continue;
        presented[d.targetFile][d.targetSymbol] = 1;
      }
    }
  }

  bool expandFragments;
  if (o.expand == Expand::Always) expandFragments = true;
  else if (o.expand == Expand::Never) expandFragments = false;
  else expandFragments = !all || a.meta.kwrds != "*";

  std::vector<Part> parts;
  uint32_t totalPresented = 0;
  std::vector<char> fileHasContent(nf, 0);
  uint32_t storedFragments = 0;

  for (size_t f = 0; f < nf; ++f) {
    const FileRecord& file = a.files[f];
    bool any = false;
    for (size_t s = 0; s < file.symbols.size(); ++s) {
      if (!presented[f][s]) continue;
      any = true;
      ++totalPresented;
      const Symbol& sym = file.symbols[s];
      const bool isMatch = matched[f][s] != 0 && (sym.selected || !all);
      std::string head = std::string("  ") + (sym.selected ? "* " : "  ") + kindName(sym.kind) + ": " +
                         sym.signature + lineRange(sym, lines);
      if (sym.parentPlus1 > 0) head += " in " + file.symbols[sym.parentPlus1 - 1].qualified;
      for (const auto& d : sym.decorators) head += " " + d;
      if (sym.exported == Tri::Yes) head += " (exported)";
      head += "\n";
      Part sig;
      sig.file = static_cast<int>(f);
      sig.section = 2;
      sig.sym = static_cast<int>(s);
      sig.slot = 0;
      sig.tier = isMatch ? 1 : 6;
      sig.text = head;
      parts.push_back(sig);

      if (isMatch && !sym.doc.empty()) {
        Part doc;
        doc.file = sig.file;
        doc.section = 2;
        doc.sym = sig.sym;
        doc.slot = 1;
        doc.tier = 4;
        doc.text = "      doc: " + firstParagraph(sym.doc, 400) + "\n";
        parts.push_back(doc);
      }
      if (isMatch && o.deps && (!all || o.expand == Expand::Always)) {
        std::string depText;
        for (const auto& d : file.deps) {
          if (d.from != s) continue;
          if (!depText.empty()) depText += "; ";
          depText += std::string(depKindName(d.kind)) + " " + d.name + " -> " + depTarget(a, file, d);
        }
        if (!depText.empty()) {
          Part dp;
          dp.file = sig.file;
          dp.section = 2;
          dp.sym = sig.sym;
          dp.slot = 2;
          dp.tier = 2;
          dp.text = "      deps: " + depText + "\n";
          parts.push_back(dp);
        }
      }
      if (sym.hasFragment) {
        ++storedFragments;
        res.fragmentsAvailable++;
        if (isMatch && expandFragments) {
          Part fr;
          fr.file = sig.file;
          fr.section = 2;
          fr.sym = sig.sym;
          fr.slot = 3;
          fr.tier = 5;
          fr.isFragment = true;
          fr.text = "      source:\n" + indentLines(sym.fragment, "        ");
          parts.push_back(fr);
        }
      }
    }
    if (any) {
      fileHasContent[f] = 1;
      Part hd;
      hd.file = static_cast<int>(f);
      hd.section = 0;
      hd.tier = 0;
      hd.text = "FILE " + file.path + " [" + file.language + "]";
      if (file.hasHash) hd.text += " sha256=" + toHex(file.hash).substr(0, 16);
      hd.text += " symbols=" + std::to_string(file.symbols.size()) + "/" + std::to_string(file.totalSymbols) + "\n";
      parts.push_back(hd);
      if (o.imports && !file.imports.empty()) {
        std::string t = "  imports:\n";
        std::set<std::string> printed;
        for (const auto& imp : file.imports) {
          std::string key = std::to_string(imp.line) + "|" + imp.text;
          if (!printed.insert(key).second) continue;
          std::string text = imp.text;
          for (char& c : text) {
            if (c == '\n') c = ' ';
          }
          t += "    ";
          if (lines && imp.line > 0) t += "L" + std::to_string(imp.line) + ": ";
          t += text + "\n";
        }
        Part ip;
        ip.file = static_cast<int>(f);
        ip.section = 1;
        ip.tier = 3;
        ip.text = t;
        parts.push_back(ip);
      }
    }
  }

  std::ostringstream preamble;
  preamble << "MFDP v" << a.meta.versionMajor << "." << a.meta.versionMinor
           << " mode=" << modeName(a.meta.resolvedMode) << " files=" << nf << " symbols="
           << totalPresented << "/" << res.symbolsStored << " (* = selected, others = context)\n";
  std::string head = preamble.str();
  uint64_t used = estimateTokens(head);

  for (auto& p : parts) p.tokens = estimateTokens(p.text);
  std::vector<size_t> orderIdx(parts.size());
  for (size_t i = 0; i < parts.size(); ++i) orderIdx[i] = i;
  std::stable_sort(orderIdx.begin(), orderIdx.end(), [&](size_t x, size_t y) {
    if (parts[x].tier != parts[y].tier) return parts[x].tier < parts[y].tier;
    if (parts[x].tier == 5) return parts[x].tokens < parts[y].tokens;
    return false;
  });
  uint64_t omittedFragmentsTokens = 0;
  uint32_t omittedFragments = 0;
  uint32_t omittedDocs = 0, omittedDeps = 0, omittedImports = 0, omittedSyms = 0;
  uint64_t omittedOtherTokens = 0;
  for (size_t idx : orderIdx) {
    Part& p = parts[idx];
    bool fits = budget == 0 || used + p.tokens <= budget;
    if (p.tier == 0) fits = true;
    if (fits) {
      p.include = true;
      used += p.tokens;
      if (p.isFragment) ++res.fragmentsIncluded;
    } else {
      if (p.isFragment) {
        ++omittedFragments;
        omittedFragmentsTokens += p.tokens;
      } else {
        omittedOtherTokens += p.tokens;
        if (p.tier == 4) ++omittedDocs;
        else if (p.tier == 2) ++omittedDeps;
        else if (p.tier == 3) ++omittedImports;
        else ++omittedSyms;
      }
    }
  }
  res.fragmentsOmitted = omittedFragments;
  res.omittedTokens = omittedFragmentsTokens + omittedOtherTokens;
  if (budget > 0 && used > budget) {
    res.warnings.push_back({"budget_exceeded_minimum",
                            "file headers alone (~" + std::to_string(used) + " tokens) exceed budget " +
                                std::to_string(budget)});
  }
  if (omittedSyms > 0) {
    res.warnings.push_back({"budget_omitted_symbols",
                            std::to_string(omittedSyms) + " symbol signature(s) omitted to fit budget " +
                                std::to_string(budget)});
  }
  if (omittedImports > 0) {
    res.warnings.push_back({"budget_omitted_imports",
                            std::to_string(omittedImports) + " import block(s) omitted to fit budget"});
  }
  if (omittedDeps > 0) {
    res.warnings.push_back({"budget_omitted_deps",
                            std::to_string(omittedDeps) + " dependency summary line(s) omitted to fit budget"});
  }
  if (omittedDocs > 0) {
    res.warnings.push_back({"budget_omitted_docs",
                            std::to_string(omittedDocs) + " doc summary line(s) omitted to fit budget"});
  }
  if (omittedFragments > 0) {
    res.warnings.push_back({"budget_omitted_source",
                            std::to_string(omittedFragments) + " source fragment(s) (~" +
                                std::to_string(omittedFragmentsTokens) + " tokens) omitted to fit budget"});
  }

  std::stable_sort(parts.begin(), parts.end(), [](const Part& x, const Part& y) {
    if (x.file != y.file) return x.file < y.file;
    if (x.section != y.section) return x.section < y.section;
    if (x.sym != y.sym) return x.sym < y.sym;
    return x.slot < y.slot;
  });
  std::string text = head;
  std::set<std::pair<int, int>> symbolsShown;
  for (const auto& p : parts) {
    if (!p.include) continue;
    if (p.section == 2 && p.slot == 0) symbolsShown.insert({p.file, p.sym});
    text += p.text;
  }
  res.symbolsPresented = static_cast<uint32_t>(symbolsShown.size());
  if (!expandFragments && storedFragments > 0) {
    text += "NOTE: " + std::to_string(storedFragments) +
            " source fragment(s) are stored; request symbols by name or use expand=always to include them\n";
  }
  res.text = text;
  res.tokens = estimateTokens(text);
  return res;
}

}
