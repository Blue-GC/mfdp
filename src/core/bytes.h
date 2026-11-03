#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mfdp/errors.h"

namespace mfdp {

class ByteWriter {
 public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void varint(uint64_t v) {
    while (v >= 0x80) {
      buf_.push_back(static_cast<uint8_t>(v | 0x80));
      v >>= 7;
    }
    buf_.push_back(static_cast<uint8_t>(v));
  }
  void raw(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
  void str(const std::string& s) {
    varint(s.size());
    raw(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  }
  std::vector<uint8_t>& data() { return buf_; }

 private:
  std::vector<uint8_t> buf_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

  size_t remaining() const { return size_ - pos_; }
  bool atEnd() const { return pos_ == size_; }

  uint8_t u8() {
    need(1);
    return data_[pos_++];
  }

  uint64_t varint() {
    uint64_t result = 0;
    for (int shift = 0; shift <= 63; shift += 7) {
      uint8_t b = u8();
      if (shift == 63 && b > 1) fail("varint overflow");
      result |= static_cast<uint64_t>(b & 0x7F) << shift;
      if (!(b & 0x80)) return result;
    }
    fail("varint too long");
    return 0;
  }

  uint32_t varint32() {
    uint64_t v = varint();
    if (v > 0xFFFFFFFFull) fail("integer out of range");
    return static_cast<uint32_t>(v);
  }

  size_t count(size_t minBytesPerElement) {
    uint64_t n = varint();
    if (minBytesPerElement == 0) minBytesPerElement = 1;
    if (n > remaining() / minBytesPerElement) fail("element count exceeds remaining data");
    return static_cast<size_t>(n);
  }

  std::string str() {
    uint64_t n = varint();
    if (n > remaining()) fail("string length exceeds remaining data");
    std::string out(reinterpret_cast<const char*>(data_ + pos_), static_cast<size_t>(n));
    pos_ += static_cast<size_t>(n);
    return out;
  }

  void raw(uint8_t* out, size_t n) {
    need(n);
    for (size_t i = 0; i < n; ++i) out[i] = data_[pos_ + i];
    pos_ += n;
  }

  [[noreturn]] void fail(const std::string& why) const {
    throw Error(ErrorCode::CorruptArtifact, "malformed artifact payload: " + why);
  }

 private:
  void need(size_t n) const {
    if (n > size_ - pos_) fail("unexpected end of payload");
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_;
};

}
