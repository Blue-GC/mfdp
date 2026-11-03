#pragma once

#include <vector>

#include "parsed.h"

namespace mfdp {

std::vector<std::vector<Dependency>> resolveDependencies(const std::vector<const ParsedFile*>& group);

}
