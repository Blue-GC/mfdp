#include "mfdp/tokens.h"

namespace mfdp {

namespace {
bool wordByte(unsigned char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
         c >= 0x80;
}
}

uint64_t estimateTokens(const std::string& text) {
  uint64_t tokens = 0;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (wordByte(c)) {
      size_t j = i;
      while (j < n && wordByte(static_cast<unsigned char>(text[j]))) ++j;
      tokens += (j - i + 3) / 4;
      i = j;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
    } else {
      ++tokens;
      ++i;
    }
  }
  return tokens;
}

}
