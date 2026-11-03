#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mfdp/errors.h"
#include "mfdp/languages.h"
#include "mfdp/tokens.h"
#include "mfdp/types.h"

namespace mfdp {

std::vector<std::string> splitList(const std::string& text);
std::string baseName(const std::string& path);
std::string toHex(const Hash& hash);
Hash sha256(const std::string& data);

std::vector<Artifact> buildArtifacts(const WriteOptions& options, std::vector<Warning>* warnings = nullptr);
std::vector<uint8_t> serialize(const Artifact& artifact);
Artifact deserialize(const std::vector<uint8_t>& bytes, const ReadLimits& limits = ReadLimits());

WriteResult write(const WriteOptions& options);

Artifact read(const std::string& src, const ReadLimits& limits = ReadLimits());
InspectResult inspect(const Artifact& artifact);
InspectResult inspect(const std::string& src);
std::string formatInspect(const InspectResult& result);

ExtractResult extract(const Artifact& artifact, const ExtractOptions& options = ExtractOptions());
ExtractResult extract(const std::string& src, const ExtractOptions& options = ExtractOptions());
ExtractResult extract(const std::string& src, const std::string& kwrds);

VerifyReport verify(const Artifact& artifact, const std::vector<OriginalSource>& originals);
VerifyReport verify(const std::string& src, const std::vector<std::string>& originalPaths);
void requireFresh(const VerifyReport& report);
std::string formatVerify(const VerifyReport& report);

}
