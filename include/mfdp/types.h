#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mfdp {

constexpr uint16_t kFormatMajor = 1;
constexpr uint16_t kFormatMinor = 0;

constexpr uint32_t kFlagHashes = 1u << 0;
constexpr uint32_t kFlagLines = 1u << 1;
constexpr uint32_t kFlagImports = 1u << 2;
constexpr uint32_t kFlagTypes = 1u << 3;
constexpr uint32_t kFlagDeps = 1u << 4;
constexpr uint32_t kFlagCombined = 1u << 5;
constexpr uint32_t kFlagFragments = 1u << 6;
constexpr uint32_t kKnownFlags = (1u << 7) - 1;

enum class SymbolKind : uint8_t {
  Function = 0,
  Method = 1,
  Constructor = 2,
  Class = 3,
  Interface = 4,
  TypeAlias = 5,
  Enum = 6,
  Variable = 7,
  Constant = 8,
  Namespace = 9,
  Property = 10
};
constexpr uint8_t kSymbolKindMax = 10;

enum class Mode : uint8_t { Auto = 0, Symbols = 1, Full = 2 };
enum class ImportMode : uint8_t { Relevant = 0, All = 1, None = 2 };
enum class DepKind : uint8_t { Call = 0, Ref = 1, Base = 2 };
enum class DepStatus : uint8_t { SameFile = 0, CrossFile = 1, External = 2, Unresolved = 3 };
enum class Tri : uint8_t { No = 0, Yes = 1, Unknown = 2 };
enum class Expand : uint8_t { Auto = 0, Always = 1, Never = 2 };

const char* kindName(SymbolKind kind);
bool parseKind(const std::string& name, SymbolKind& out);
bool isTypeKind(SymbolKind kind);
bool isContainerKind(SymbolKind kind);
const char* modeName(Mode mode);
bool parseMode(const std::string& name, Mode& out);
const char* importModeName(ImportMode mode);
bool parseImportMode(const std::string& name, ImportMode& out);
const char* depKindName(DepKind kind);
const char* depStatusName(DepStatus status);
const char* triName(Tri value);
bool parseBool(const std::string& text, bool& out);

using Hash = std::array<uint8_t, 32>;

struct Warning {
  std::string code;
  std::string message;
};

struct Param {
  std::string name;
  std::string annotation;
  std::string defaultValue;
};

struct Symbol {
  SymbolKind kind = SymbolKind::Function;
  uint32_t parentPlus1 = 0;
  Tri exported = Tri::Unknown;
  bool selected = false;
  bool context = false;
  bool hasFragment = false;
  bool coveredByAncestor = false;
  std::string name;
  std::string qualified;
  std::string signature;
  std::string returnType;
  std::string doc;
  std::string fragment;
  std::vector<Param> params;
  std::vector<std::string> decorators;
  std::vector<std::string> bases;
  uint32_t startLine = 0;
  uint32_t endLine = 0;
};

struct Binding {
  std::string local;
  std::string imported;
  std::string module;
  uint32_t depth = 1;
};

struct Import {
  uint32_t line = 0;
  uint32_t endLine = 0;
  std::string text;
  std::string module;
  bool star = false;
  std::vector<Binding> bindings;
};

struct Dependency {
  uint32_t from = 0;
  DepKind kind = DepKind::Call;
  DepStatus status = DepStatus::Unresolved;
  std::string name;
  std::string module;
  int32_t targetFile = -1;
  int32_t targetSymbol = -1;
};

struct FileRecord {
  std::string path;
  std::string filename;
  std::string language;
  uint64_t sourceSize = 0;
  bool hasHash = false;
  Hash hash{};
  uint32_t totalSymbols = 0;
  uint32_t totalImports = 0;
  std::vector<std::string> exports;
  std::vector<Import> imports;
  std::vector<Symbol> symbols;
  std::vector<Dependency> deps;
};

struct ArtifactMeta {
  uint16_t versionMajor = kFormatMajor;
  uint16_t versionMinor = kFormatMinor;
  uint32_t flags = 0;
  uint8_t compression = 0;
  uint64_t storedBytes = 0;
  uint64_t rawBytes = 0;
  Mode requestedMode = Mode::Auto;
  Mode resolvedMode = Mode::Symbols;
  ImportMode importMode = ImportMode::Relevant;
  bool combined = true;
  uint32_t budget = 0;
  uint32_t depDepth = 1;
  std::string kwrds = "*";
  std::vector<Warning> warnings;
};

struct Artifact {
  ArtifactMeta meta;
  std::vector<FileRecord> files;
};

struct MemorySource {
  std::string filename;
  std::string content;
};

struct WriteOptions {
  std::vector<std::string> src;
  std::vector<MemorySource> memory;
  std::string kwrds = "*";
  std::optional<bool> inc;
  std::optional<bool> include;
  std::optional<bool> inctyps;
  std::optional<bool> include_types;
  std::optional<ImportMode> import_mode;
  bool legacy_inctyps = false;
  uint32_t budget = 0;
  std::string lang = "auto";
  bool hash = true;
  Mode mode = Mode::Auto;
  bool deps = true;
  uint32_t dep_depth = 1;
  bool lines = true;
  bool combined = true;
  std::string output;
  std::string output_dir;
  bool overwrite = false;
  uint64_t max_source_bytes = 32ull * 1024 * 1024;
};

struct ArtifactSummary {
  std::string output;
  uint64_t storedBytes = 0;
  uint64_t rawBytes = 0;
  uint32_t files = 0;
  uint32_t symbolsStored = 0;
  uint32_t symbolsParsed = 0;
};

struct WriteResult {
  std::vector<std::string> outputs;
  std::vector<ArtifactSummary> summaries;
  std::vector<Warning> warnings;
};

struct ReadLimits {
  uint64_t maxFileBytes = 256ull * 1024 * 1024;
  uint64_t maxPayloadBytes = 256ull * 1024 * 1024;
};

struct ExtractOptions {
  std::string kwrds = "*";
  std::optional<uint32_t> budget;
  Expand expand = Expand::Auto;
  bool deps = true;
  bool imports = true;
};

struct ExtractResult {
  std::string text;
  uint64_t tokens = 0;
  uint32_t budget = 0;
  uint32_t symbolsStored = 0;
  uint32_t symbolsPresented = 0;
  uint32_t fragmentsAvailable = 0;
  uint32_t fragmentsIncluded = 0;
  uint32_t fragmentsOmitted = 0;
  uint64_t omittedTokens = 0;
  std::vector<Warning> warnings;
};

struct FileInspect {
  std::string path;
  std::string filename;
  std::string language;
  uint64_t sourceSize = 0;
  bool hasHash = false;
  std::string hashHex;
  uint32_t totalSymbols = 0;
  uint32_t storedSymbols = 0;
  uint32_t selectedSymbols = 0;
  uint32_t totalImports = 0;
  uint32_t storedImports = 0;
  uint32_t dependencies = 0;
  uint32_t fragments = 0;
};

struct InspectResult {
  uint16_t versionMajor = 0;
  uint16_t versionMinor = 0;
  uint32_t flags = 0;
  uint8_t compression = 0;
  uint64_t storedBytes = 0;
  uint64_t rawBytes = 0;
  Mode requestedMode = Mode::Auto;
  Mode resolvedMode = Mode::Symbols;
  ImportMode importMode = ImportMode::Relevant;
  bool combined = true;
  uint32_t budget = 0;
  uint32_t depDepth = 1;
  std::string kwrds;
  std::vector<FileInspect> files;
  std::vector<Warning> warnings;
};

enum class VerifyStatus : uint8_t { Match = 0, Stale = 1, NoHashStored = 2, MissingOriginal = 3 };

struct VerifyEntry {
  std::string recordPath;
  std::string originalPath;
  VerifyStatus status = VerifyStatus::MissingOriginal;
  std::string expectedHex;
  std::string actualHex;
};

struct VerifyReport {
  bool ok = false;
  std::vector<VerifyEntry> entries;
  std::vector<std::string> unmatchedOriginals;
};

struct OriginalSource {
  std::string path;
  std::string content;
};

}
