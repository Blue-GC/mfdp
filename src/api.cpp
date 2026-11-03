#include <fcntl.h>
#if defined(__linux__)
#include <sys/random.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "context.h"
#include "mfdp/mfdp.h"
#include "sha256.h"

namespace mfdp {

std::vector<std::string> splitList(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuote = false;
  bool any = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '\\' && i + 1 < text.size() && (text[i + 1] == ',' || text[i + 1] == '\\' || text[i + 1] == '"')) {
      cur.push_back(text[i + 1]);
      ++i;
      any = true;
    } else if (c == '"') {
      inQuote = !inQuote;
      any = true;
    } else if (c == ',' && !inQuote) {
      if (any) out.push_back(cur);
      cur.clear();
      any = false;
    } else {
      cur.push_back(c);
      any = true;
    }
  }
  if (inQuote) throw Error(ErrorCode::InvalidArgument, "unterminated quote in path list");
  if (any) out.push_back(cur);
  return out;
}

std::string baseName(const std::string& path) {
  std::string p = path;
  while (p.size() > 1 && p.back() == '/') p.pop_back();
  size_t pos = p.find_last_of('/');
  return pos == std::string::npos ? p : p.substr(pos + 1);
}

namespace {

std::string randomHex32() {
  uint8_t buf[16];
  size_t got = 0;
#if defined(__linux__)
  while (got < sizeof(buf)) {
    ssize_t n = ::getrandom(buf + got, sizeof(buf) - got, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    got += static_cast<size_t>(n);
  }
#endif
  if (got < sizeof(buf)) {
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) throw Error(ErrorCode::Io, "no secure random source available");
    while (got < sizeof(buf)) {
      ssize_t r = ::read(fd, buf + got, sizeof(buf) - got);
      if (r <= 0) {
        ::close(fd);
        throw Error(ErrorCode::Io, "failed to read secure random bytes");
      }
      got += static_cast<size_t>(r);
    }
    ::close(fd);
  }
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (uint8_t b : buf) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 15]);
  }
  return out;
}

bool isDirectory(const std::string& p) {
  struct stat st;
  return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool writeAll(int fd, const std::vector<uint8_t>& bytes) {
  size_t off = 0;
  while (off < bytes.size()) {
    ssize_t n = ::write(fd, bytes.data() + off, bytes.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return ::fsync(fd) == 0;
}

void writeExclusive(const std::string& path, const std::vector<uint8_t>& bytes, bool& existed) {
  existed = false;
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
  if (fd < 0) {
    if (errno == EEXIST) {
      existed = true;
      return;
    }
    throw Error(ErrorCode::Io, std::string("cannot create output file: ") + std::strerror(errno), path);
  }
  bool ok = writeAll(fd, bytes);
  ::close(fd);
  if (!ok) {
    ::unlink(path.c_str());
    throw Error(ErrorCode::Io, "failed to write output file", path);
  }
}

void writeReplacing(const std::string& path, const std::vector<uint8_t>& bytes) {
  for (int attempt = 0; attempt < 16; ++attempt) {
    std::string tmp = path + ".tmp-" + randomHex32().substr(0, 12);
    bool existed = false;
    writeExclusive(tmp, bytes, existed);
    if (existed) continue;
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
      ::unlink(tmp.c_str());
      throw Error(ErrorCode::Io, std::string("cannot replace output file: ") + std::strerror(errno), path);
    }
    return;
  }
  throw Error(ErrorCode::Io, "could not create a temporary output file", path);
}

std::string validateExplicitOutput(const std::string& p) {
  if (p.empty()) throw Error(ErrorCode::InvalidArgument, "empty output path");
  if (p.find('\0') != std::string::npos) throw Error(ErrorCode::InvalidArgument, "output path contains NUL");
  if (isDirectory(p)) throw Error(ErrorCode::InvalidArgument, "output path is a directory", p);
  std::filesystem::path parent = std::filesystem::path(p).parent_path();
  if (!parent.empty() && !isDirectory(parent.string())) {
    throw Error(ErrorCode::Io, "output directory does not exist", parent.string());
  }
  return p;
}

std::string joinDir(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (dir.back() == '/') return dir + name;
  return dir + "/" + name;
}

std::string storeArtifact(const std::vector<uint8_t>& bytes, const std::string& explicitPath,
                          const std::string& dir, bool overwrite) {
  if (!explicitPath.empty()) {
    validateExplicitOutput(explicitPath);
    if (overwrite) {
      writeReplacing(explicitPath, bytes);
      return explicitPath;
    }
    bool existed = false;
    writeExclusive(explicitPath, bytes, existed);
    if (existed) throw Error(ErrorCode::OutputExists, "output file already exists", explicitPath);
    return explicitPath;
  }
  if (!dir.empty() && !isDirectory(dir)) throw Error(ErrorCode::Io, "output directory does not exist", dir);
  for (int attempt = 0; attempt < 64; ++attempt) {
    std::string path = joinDir(dir, randomHex32() + ".mfdp");
    bool existed = false;
    writeExclusive(path, bytes, existed);
    if (!existed) return path;
  }
  throw Error(ErrorCode::Io, "could not allocate a unique output filename");
}

std::vector<uint8_t> readFileBytes(const std::string& path, uint64_t maxBytes) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) throw Error(ErrorCode::Io, "cannot access file", path);
  if (!S_ISREG(st.st_mode)) throw Error(ErrorCode::InvalidArgument, "not a regular file", path);
  if (static_cast<uint64_t>(st.st_size) > maxBytes) throw Error(ErrorCode::TooLarge, "file exceeds size limit", path);
  std::ifstream in(path, std::ios::binary);
  if (!in) throw Error(ErrorCode::Io, "cannot open file", path);
  std::vector<uint8_t> data(static_cast<size_t>(st.st_size));
  if (!data.empty()) in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!in && !data.empty()) throw Error(ErrorCode::Io, "failed to read file", path);
  return data;
}

}

WriteResult write(const WriteOptions& options) {
  WriteResult result;
  std::vector<Artifact> artifacts = buildArtifacts(options, &result.warnings);
  std::vector<std::string> outputs;
  if (!options.output.empty()) outputs = splitList(options.output);
  bool outputIsDir = outputs.size() == 1 && (isDirectory(outputs[0]) || outputs[0].back() == '/');
  if (!outputs.empty() && !outputIsDir && outputs.size() != artifacts.size()) {
    throw Error(ErrorCode::InvalidArgument,
                "output lists " + std::to_string(outputs.size()) + " name(s) but " +
                    std::to_string(artifacts.size()) + " artifact(s) will be produced");
  }
  std::vector<std::vector<uint8_t>> encoded;
  for (const auto& a : artifacts) encoded.push_back(serialize(a));
  for (size_t i = 0; i < artifacts.size(); ++i) {
    std::string explicitPath;
    std::string dir = options.output_dir;
    if (outputIsDir) dir = outputs[0];
    else if (!outputs.empty()) explicitPath = outputs[i];
    std::string stored = storeArtifact(encoded[i], explicitPath, dir, options.overwrite);
    ArtifactSummary sum;
    sum.output = stored;
    sum.storedBytes = encoded[i].size();
    Artifact reread = deserialize(encoded[i]);
    sum.rawBytes = reread.meta.rawBytes;
    sum.files = static_cast<uint32_t>(artifacts[i].files.size());
    for (const auto& f : artifacts[i].files) {
      sum.symbolsStored += static_cast<uint32_t>(f.symbols.size());
      sum.symbolsParsed += f.totalSymbols;
    }
    result.outputs.push_back(stored);
    result.summaries.push_back(sum);
  }
  return result;
}

Artifact read(const std::string& src, const ReadLimits& limits) {
  std::vector<uint8_t> bytes = readFileBytes(src, limits.maxFileBytes);
  return deserialize(bytes, limits);
}

InspectResult inspect(const Artifact& a) {
  InspectResult r;
  r.versionMajor = a.meta.versionMajor;
  r.versionMinor = a.meta.versionMinor;
  r.flags = a.meta.flags;
  r.compression = a.meta.compression;
  r.storedBytes = a.meta.storedBytes;
  r.rawBytes = a.meta.rawBytes;
  r.requestedMode = a.meta.requestedMode;
  r.resolvedMode = a.meta.resolvedMode;
  r.importMode = a.meta.importMode;
  r.combined = a.meta.combined;
  r.budget = a.meta.budget;
  r.depDepth = a.meta.depDepth;
  r.kwrds = a.meta.kwrds;
  r.warnings = a.meta.warnings;
  for (const auto& f : a.files) {
    FileInspect fi;
    fi.path = f.path;
    fi.filename = f.filename;
    fi.language = f.language;
    fi.sourceSize = f.sourceSize;
    fi.hasHash = f.hasHash;
    if (f.hasHash) fi.hashHex = toHex(f.hash);
    fi.totalSymbols = f.totalSymbols;
    fi.storedSymbols = static_cast<uint32_t>(f.symbols.size());
    for (const auto& s : f.symbols) {
      if (s.selected) ++fi.selectedSymbols;
      if (s.hasFragment) ++fi.fragments;
    }
    fi.totalImports = f.totalImports;
    fi.storedImports = static_cast<uint32_t>(f.imports.size());
    fi.dependencies = static_cast<uint32_t>(f.deps.size());
    r.files.push_back(std::move(fi));
  }
  return r;
}

InspectResult inspect(const std::string& src) { return inspect(read(src)); }

std::string formatInspect(const InspectResult& r) {
  std::ostringstream o;
  o << "format: " << r.versionMajor << "." << r.versionMinor << " flags=0x" << std::hex << r.flags
    << std::dec << " compression=" << (r.compression == 1 ? "zlib" : "none") << " stored="
    << r.storedBytes << "B raw=" << r.rawBytes << "B\n";
  o << "mode: requested=" << modeName(r.requestedMode) << " resolved=" << modeName(r.resolvedMode)
    << " imports=" << importModeName(r.importMode) << " combined=" << (r.combined ? "true" : "false")
    << " dep_depth=" << r.depDepth << " budget=" << r.budget << " kwrds=" << r.kwrds << "\n";
  o << "flags:" << ((r.flags & kFlagHashes) ? " hashes" : "") << ((r.flags & kFlagLines) ? " lines" : "")
    << ((r.flags & kFlagImports) ? " imports" : "") << ((r.flags & kFlagTypes) ? " types" : "")
    << ((r.flags & kFlagDeps) ? " deps" : "") << ((r.flags & kFlagCombined) ? " combined" : "")
    << ((r.flags & kFlagFragments) ? " fragments" : "") << "\n";
  for (const auto& f : r.files) {
    o << "file: " << f.path << " (" << f.filename << ") language=" << f.language << " size="
      << f.sourceSize << " sha256=" << (f.hasHash ? f.hashHex : std::string("none")) << "\n";
    o << "  symbols: stored=" << f.storedSymbols << " selected=" << f.selectedSymbols << " parsed="
      << f.totalSymbols << " fragments=" << f.fragments << "\n";
    o << "  imports: stored=" << f.storedImports << " parsed=" << f.totalImports << " deps="
      << f.dependencies << "\n";
  }
  for (const auto& w : r.warnings) o << "warning[" << w.code << "]: " << w.message << "\n";
  return o.str();
}

ExtractResult extract(const Artifact& artifact, const ExtractOptions& options) {
  return renderContext(artifact, options);
}

ExtractResult extract(const std::string& src, const ExtractOptions& options) {
  return renderContext(read(src), options);
}

ExtractResult extract(const std::string& src, const std::string& kwrds) {
  ExtractOptions o;
  o.kwrds = kwrds;
  return extract(src, o);
}

VerifyReport verify(const Artifact& artifact, const std::vector<OriginalSource>& originals) {
  VerifyReport report;
  std::vector<char> used(originals.size(), 0);
  report.ok = true;
  for (const auto& rec : artifact.files) {
    VerifyEntry e;
    e.recordPath = rec.path;
    int match = -1;
    for (size_t i = 0; i < originals.size(); ++i) {
      if (std::filesystem::path(originals[i].path).lexically_normal().generic_string() == rec.path) {
        match = static_cast<int>(i);
        break;
      }
    }
    if (match < 0) {
      int count = 0;
      for (size_t i = 0; i < originals.size(); ++i) {
        if (!used[i] && baseName(originals[i].path) == rec.filename) {
          match = static_cast<int>(i);
          ++count;
        }
      }
      if (count > 1) match = -1;
    }
    if (match < 0) {
      e.status = VerifyStatus::MissingOriginal;
      report.ok = false;
    } else {
      used[match] = 1;
      e.originalPath = originals[match].path;
      Hash actual = sha256(originals[match].content);
      e.actualHex = toHex(actual);
      if (!rec.hasHash) {
        e.status = VerifyStatus::NoHashStored;
        report.ok = false;
      } else {
        e.expectedHex = toHex(rec.hash);
        if (actual == rec.hash && originals[match].content.size() == rec.sourceSize) {
          e.status = VerifyStatus::Match;
        } else {
          e.status = VerifyStatus::Stale;
          report.ok = false;
        }
      }
    }
    report.entries.push_back(std::move(e));
  }
  for (size_t i = 0; i < originals.size(); ++i) {
    if (!used[i]) report.unmatchedOriginals.push_back(originals[i].path);
  }
  return report;
}

VerifyReport verify(const std::string& src, const std::vector<std::string>& originalPaths) {
  Artifact a = read(src);
  std::vector<OriginalSource> originals;
  for (const auto& p : originalPaths) {
    std::vector<uint8_t> bytes = readFileBytes(p, 256ull * 1024 * 1024);
    OriginalSource o;
    o.path = p;
    o.content.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    originals.push_back(std::move(o));
  }
  return verify(a, originals);
}

void requireFresh(const VerifyReport& report) {
  for (const auto& e : report.entries) {
    if (e.status == VerifyStatus::Stale) {
      throw Error(ErrorCode::StaleSource, "source has changed since the artifact was generated", e.recordPath);
    }
    if (e.status == VerifyStatus::NoHashStored) {
      throw Error(ErrorCode::StaleSource, "artifact has no stored hash; freshness cannot be established", e.recordPath);
    }
    if (e.status == VerifyStatus::MissingOriginal) {
      throw Error(ErrorCode::StaleSource, "no original source supplied for artifact record", e.recordPath);
    }
  }
}

std::string formatVerify(const VerifyReport& report) {
  std::ostringstream o;
  for (const auto& e : report.entries) {
    const char* label = "MATCH";
    if (e.status == VerifyStatus::Stale) label = "STALE";
    else if (e.status == VerifyStatus::NoHashStored) label = "NO-HASH";
    else if (e.status == VerifyStatus::MissingOriginal) label = "MISSING";
    o << label << " " << e.recordPath;
    if (!e.originalPath.empty()) o << " <- " << e.originalPath;
    if (e.status == VerifyStatus::Stale) o << " expected=" << e.expectedHex << " actual=" << e.actualHex;
    o << "\n";
  }
  for (const auto& u : report.unmatchedOriginals) o << "UNMATCHED " << u << "\n";
  o << (report.ok ? "verify: ok" : "verify: FAILED") << "\n";
  return o.str();
}

}
