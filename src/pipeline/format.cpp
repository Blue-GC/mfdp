#include <zlib.h>

#include <cstring>

#include "bytes.h"
#include "mfdp/mfdp.h"
#include "sha256.h"

namespace mfdp {

namespace {

constexpr size_t kHeaderSize = 72;
constexpr uint8_t kMagic[8] = {0x89, 'M', 'F', 'D', 'P', 0x0D, 0x0A, 0x1A};
constexpr uint32_t kMaxZlibRatio = 1100;

void putU16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void putU32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void putU64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t getU32(const uint8_t* p) {
  uint32_t x = 0;
  for (int i = 0; i < 4; ++i) x |= static_cast<uint32_t>(p[i]) << (8 * i);
  return x;
}
uint64_t getU64(const uint8_t* p) {
  uint64_t x = 0;
  for (int i = 0; i < 8; ++i) x |= static_cast<uint64_t>(p[i]) << (8 * i);
  return x;
}

}

std::vector<uint8_t> serialize(const Artifact& a) {
  const ArtifactMeta& m = a.meta;
  bool anyHash = false;
  bool anyFragment = false;
  for (const auto& f : a.files) {
    if (f.hasHash) anyHash = true;
    for (const auto& s : f.symbols) {
      if (s.hasFragment) anyFragment = true;
    }
  }
  uint32_t flags = m.flags & ~(kFlagHashes | kFlagFragments);
  if (anyHash) flags |= kFlagHashes;
  if (anyFragment) flags |= kFlagFragments;
  const bool lines = (flags & kFlagLines) != 0;

  ByteWriter w;
  w.u8(static_cast<uint8_t>(m.requestedMode));
  w.u8(static_cast<uint8_t>(m.resolvedMode));
  w.u8(static_cast<uint8_t>(m.importMode));
  w.u8(m.combined ? 1 : 0);
  w.varint(m.budget);
  w.varint(m.depDepth);
  w.str(m.kwrds);
  w.varint(m.warnings.size());
  for (const auto& wn : m.warnings) {
    w.str(wn.code);
    w.str(wn.message);
  }
  w.varint(a.files.size());
  for (const auto& f : a.files) {
    w.str(f.path);
    w.str(f.filename);
    w.str(f.language);
    w.varint(f.sourceSize);
    w.u8(f.hasHash ? 1 : 0);
    if (f.hasHash) w.raw(f.hash.data(), f.hash.size());
    w.varint(f.totalSymbols);
    w.varint(f.totalImports);
    w.varint(f.exports.size());
    for (const auto& e : f.exports) w.str(e);
    w.varint(f.imports.size());
    for (const auto& imp : f.imports) {
      if (lines) {
        w.varint(imp.line);
        w.varint(imp.endLine);
      }
      w.str(imp.text);
      w.str(imp.module);
      w.u8(imp.star ? 1 : 0);
      w.varint(imp.bindings.size());
      for (const auto& b : imp.bindings) {
        w.str(b.local);
        w.str(b.imported);
        w.str(b.module);
      }
    }
    w.varint(f.symbols.size());
    for (const auto& s : f.symbols) {
      w.u8(static_cast<uint8_t>(s.kind));
      w.varint(s.parentPlus1);
      w.u8(static_cast<uint8_t>(s.exported));
      uint8_t sf = 0;
      if (s.selected) sf |= 1;
      if (s.context) sf |= 2;
      if (s.hasFragment) sf |= 4;
      if (s.coveredByAncestor) sf |= 8;
      w.u8(sf);
      w.str(s.name);
      w.str(s.qualified);
      w.str(s.signature);
      w.str(s.returnType);
      w.str(s.doc);
      w.varint(s.params.size());
      for (const auto& p : s.params) {
        w.str(p.name);
        w.str(p.annotation);
        w.str(p.defaultValue);
      }
      w.varint(s.decorators.size());
      for (const auto& d : s.decorators) w.str(d);
      w.varint(s.bases.size());
      for (const auto& b : s.bases) w.str(b);
      if (lines) {
        w.varint(s.startLine);
        w.varint(s.endLine);
      }
      if (s.hasFragment) w.str(s.fragment);
    }
    w.varint(f.deps.size());
    for (const auto& d : f.deps) {
      w.varint(d.from);
      w.u8(static_cast<uint8_t>(d.kind));
      w.u8(static_cast<uint8_t>(d.status));
      w.str(d.name);
      w.str(d.module);
      w.varint(static_cast<uint64_t>(static_cast<int64_t>(d.targetFile) + 1));
      w.varint(static_cast<uint64_t>(static_cast<int64_t>(d.targetSymbol) + 1));
    }
  }

  const std::vector<uint8_t>& raw = w.data();
  Hash digest = sha256Bytes(raw.data(), raw.size());
  std::vector<uint8_t> stored;
  uint8_t method = 0;
  if (!raw.empty()) {
    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> z(bound);
    uLongf outLen = bound;
    if (compress2(z.data(), &outLen, raw.data(), static_cast<uLong>(raw.size()), 9) == Z_OK &&
        outLen < raw.size()) {
      z.resize(outLen);
      stored = std::move(z);
      method = 1;
    }
  }
  if (method == 0) stored = raw;

  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + stored.size());
  out.insert(out.end(), kMagic, kMagic + 8);
  putU16(out, kFormatMajor);
  putU16(out, kFormatMinor);
  putU32(out, flags);
  out.push_back(method);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  putU64(out, stored.size());
  putU64(out, raw.size());
  out.insert(out.end(), digest.begin(), digest.end());
  uint32_t crc = static_cast<uint32_t>(crc32(0L, out.data(), static_cast<uInt>(out.size())));
  putU32(out, crc);
  out.insert(out.end(), stored.begin(), stored.end());
  return out;
}

Artifact deserialize(const std::vector<uint8_t>& bytes, const ReadLimits& limits) {
  if (bytes.size() > limits.maxFileBytes) {
    throw Error(ErrorCode::TooLarge, "artifact exceeds the configured file size limit");
  }
  if (bytes.size() < 8) throw Error(ErrorCode::TruncatedArtifact, "artifact is truncated");
  if (std::memcmp(bytes.data(), kMagic, 8) != 0) {
    throw Error(ErrorCode::CorruptArtifact, "not a mfdp artifact (bad magic)");
  }
  if (bytes.size() < kHeaderSize) throw Error(ErrorCode::TruncatedArtifact, "artifact header is truncated");
  const uint8_t* h = bytes.data();
  uint32_t expectedCrc = getU32(h + 68);
  uint32_t crc = static_cast<uint32_t>(crc32(0L, h, 68));
  if (crc != expectedCrc) throw Error(ErrorCode::CorruptArtifact, "artifact header checksum mismatch");
  uint16_t major = getU16(h + 8);
  uint16_t minor = getU16(h + 10);
  if (major != kFormatMajor || minor > kFormatMinor) {
    throw Error(ErrorCode::UnsupportedVersion, "unsupported artifact version " + std::to_string(major) +
                                                   "." + std::to_string(minor));
  }
  uint32_t flags = getU32(h + 12);
  if (flags & ~kKnownFlags) throw Error(ErrorCode::CorruptArtifact, "unknown feature flags set");
  uint8_t method = h[16];
  if (method > 1 || h[17] != 0 || h[18] != 0 || h[19] != 0) {
    throw Error(ErrorCode::CorruptArtifact, "invalid compression metadata");
  }
  uint64_t storedLen = getU64(h + 20);
  uint64_t rawLen = getU64(h + 28);
  uint64_t available = bytes.size() - kHeaderSize;
  if (storedLen > available) throw Error(ErrorCode::TruncatedArtifact, "artifact payload is truncated");
  if (storedLen < available) throw Error(ErrorCode::CorruptArtifact, "artifact has trailing data");
  if (rawLen > limits.maxPayloadBytes) {
    throw Error(ErrorCode::TooLarge, "artifact payload exceeds the configured limit");
  }
  if (method == 0 && storedLen != rawLen) {
    throw Error(ErrorCode::CorruptArtifact, "stored and raw payload lengths differ for an uncompressed artifact");
  }
  if (method == 1 && rawLen > storedLen * kMaxZlibRatio + 64) {
    throw Error(ErrorCode::CorruptArtifact, "implausible compression ratio");
  }
  std::vector<uint8_t> raw(static_cast<size_t>(rawLen));
  if (method == 0) {
    if (rawLen > 0) std::memcpy(raw.data(), h + kHeaderSize, static_cast<size_t>(rawLen));
  } else {
    uLongf destLen = static_cast<uLongf>(rawLen);
    uLong srcLen = static_cast<uLong>(storedLen);
    int rc = uncompress2(raw.data(), &destLen, h + kHeaderSize, &srcLen);
    if (rc != Z_OK || destLen != rawLen || srcLen != storedLen) {
      throw Error(ErrorCode::CorruptArtifact, "payload decompression failed");
    }
  }
  Hash digest = sha256Bytes(raw.data(), raw.size());
  if (std::memcmp(digest.data(), h + 36, 32) != 0) {
    throw Error(ErrorCode::CorruptArtifact, "payload checksum mismatch");
  }

  Artifact a;
  a.meta.versionMajor = major;
  a.meta.versionMinor = minor;
  a.meta.flags = flags;
  a.meta.compression = method;
  a.meta.storedBytes = bytes.size();
  a.meta.rawBytes = rawLen;
  const bool lines = (flags & kFlagLines) != 0;
  const bool fragments = (flags & kFlagFragments) != 0;
  const bool hashes = (flags & kFlagHashes) != 0;

  ByteReader r(raw.data(), raw.size());
  uint8_t rm = r.u8();
  uint8_t sm = r.u8();
  uint8_t im = r.u8();
  uint8_t cb = r.u8();
  if (rm > 2 || sm > 2 || im > 2 || cb > 1) r.fail("invalid mode value");
  a.meta.requestedMode = static_cast<Mode>(rm);
  a.meta.resolvedMode = static_cast<Mode>(sm);
  a.meta.importMode = static_cast<ImportMode>(im);
  a.meta.combined = cb == 1;
  a.meta.budget = r.varint32();
  a.meta.depDepth = r.varint32();
  a.meta.kwrds = r.str();
  size_t nw = r.count(2);
  for (size_t i = 0; i < nw; ++i) {
    Warning wn;
    wn.code = r.str();
    wn.message = r.str();
    a.meta.warnings.push_back(std::move(wn));
  }
  size_t nfiles = r.count(8);
  a.files.resize(nfiles);
  for (size_t fi = 0; fi < nfiles; ++fi) {
    FileRecord& f = a.files[fi];
    f.path = r.str();
    f.filename = r.str();
    f.language = r.str();
    f.sourceSize = r.varint();
    uint8_t hh = r.u8();
    if (hh > 1) r.fail("invalid hash flag");
    f.hasHash = hh == 1;
    if (f.hasHash) {
      if (!hashes) r.fail("hash present but hash flag unset");
      r.raw(f.hash.data(), f.hash.size());
    }
    f.totalSymbols = r.varint32();
    f.totalImports = r.varint32();
    size_t ne = r.count(1);
    for (size_t i = 0; i < ne; ++i) f.exports.push_back(r.str());
    size_t ni = r.count(4);
    for (size_t i = 0; i < ni; ++i) {
      Import imp;
      if (lines) {
        imp.line = r.varint32();
        imp.endLine = r.varint32();
      }
      imp.text = r.str();
      imp.module = r.str();
      uint8_t star = r.u8();
      if (star > 1) r.fail("invalid star flag");
      imp.star = star == 1;
      size_t nb = r.count(3);
      for (size_t b = 0; b < nb; ++b) {
        Binding bd;
        bd.local = r.str();
        bd.imported = r.str();
        bd.module = r.str();
        imp.bindings.push_back(std::move(bd));
      }
      f.imports.push_back(std::move(imp));
    }
    size_t ns = r.count(8);
    for (size_t si = 0; si < ns; ++si) {
      Symbol s;
      uint8_t kind = r.u8();
      if (kind > kSymbolKindMax) r.fail("invalid symbol kind");
      s.kind = static_cast<SymbolKind>(kind);
      s.parentPlus1 = r.varint32();
      if (s.parentPlus1 > si) r.fail("symbol parent does not precede child");
      uint8_t ex = r.u8();
      if (ex > 2) r.fail("invalid export flag");
      s.exported = static_cast<Tri>(ex);
      uint8_t sf = r.u8();
      if (sf > 15) r.fail("invalid symbol flags");
      s.selected = (sf & 1) != 0;
      s.context = (sf & 2) != 0;
      s.hasFragment = (sf & 4) != 0;
      s.coveredByAncestor = (sf & 8) != 0;
      if (s.hasFragment && !fragments) r.fail("fragment present but fragment flag unset");
      s.name = r.str();
      s.qualified = r.str();
      s.signature = r.str();
      s.returnType = r.str();
      s.doc = r.str();
      size_t np = r.count(3);
      for (size_t p = 0; p < np; ++p) {
        Param pr;
        pr.name = r.str();
        pr.annotation = r.str();
        pr.defaultValue = r.str();
        s.params.push_back(std::move(pr));
      }
      size_t nd = r.count(1);
      for (size_t d = 0; d < nd; ++d) s.decorators.push_back(r.str());
      size_t nbases = r.count(1);
      for (size_t b = 0; b < nbases; ++b) s.bases.push_back(r.str());
      if (lines) {
        s.startLine = r.varint32();
        s.endLine = r.varint32();
      }
      if (s.hasFragment) s.fragment = r.str();
      f.symbols.push_back(std::move(s));
    }
    size_t ndeps = r.count(7);
    for (size_t di = 0; di < ndeps; ++di) {
      Dependency d;
      d.from = r.varint32();
      if (d.from >= f.symbols.size()) r.fail("dependency source out of range");
      uint8_t kind = r.u8();
      uint8_t status = r.u8();
      if (kind > 2 || status > 3) r.fail("invalid dependency enums");
      d.kind = static_cast<DepKind>(kind);
      d.status = static_cast<DepStatus>(status);
      d.name = r.str();
      d.module = r.str();
      uint64_t tf = r.varint();
      uint64_t ts = r.varint();
      if (tf > nfiles) r.fail("dependency target file out of range");
      if (ts > 0xFFFFFFFFull) r.fail("dependency target symbol out of range");
      d.targetFile = static_cast<int32_t>(static_cast<int64_t>(tf) - 1);
      d.targetSymbol = static_cast<int32_t>(static_cast<int64_t>(ts) - 1);
      f.deps.push_back(std::move(d));
    }
  }
  if (!r.atEnd()) r.fail("trailing bytes after payload");
  for (const auto& f : a.files) {
    for (const auto& d : f.deps) {
      if (d.targetSymbol >= 0) {
        if (d.targetFile < 0 || static_cast<size_t>(d.targetFile) >= a.files.size() ||
            static_cast<size_t>(d.targetSymbol) >= a.files[d.targetFile].symbols.size()) {
          throw Error(ErrorCode::CorruptArtifact, "dependency target symbol out of range");
        }
      }
    }
  }
  return a;
}

}
