#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mfdp/types.h"

namespace mfdp {

class Sha256 {
 public:
  Sha256();
  void update(const uint8_t* data, size_t size);
  Hash finish();

 private:
  void block(const uint8_t* chunk);
  uint32_t state_[8];
  uint8_t buffer_[64];
  size_t bufferSize_;
  uint64_t total_;
};

Hash sha256Bytes(const uint8_t* data, size_t size);
bool fromHex(const std::string& text, Hash& out);

}
