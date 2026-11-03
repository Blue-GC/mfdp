#include "json.h"

#include <cstdlib>

#include "mfdp/errors.h"

namespace mfdp {
namespace json {

const Value* Value::find(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& kv : object) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

namespace {

class Parser {
 public:
  Parser(const std::string& text, size_t maxDepth) : s_(text), pos_(0), maxDepth_(maxDepth) {}

  Value run() {
    Value v = value(0);
    skip();
    if (pos_ != s_.size()) fail("trailing characters");
    return v;
  }

 private:
  [[noreturn]] void fail(const std::string& why) const {
    throw Error(ErrorCode::AdapterFailed,
                "invalid JSON from adapter at offset " + std::to_string(pos_) + ": " + why);
  }

  void skip() {
    while (pos_ < s_.size()) {
      char c = s_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
      else break;
    }
  }

  char peek() {
    if (pos_ >= s_.size()) fail("unexpected end");
    return s_[pos_];
  }

  void expect(const char* lit) {
    for (const char* p = lit; *p; ++p) {
      if (pos_ >= s_.size() || s_[pos_] != *p) fail("bad literal");
      ++pos_;
    }
  }

  Value value(size_t depth) {
    if (depth > maxDepth_) fail("nesting too deep");
    skip();
    char c = peek();
    Value v;
    if (c == '{') {
      ++pos_;
      v.type = Type::Object;
      skip();
      if (peek() == '}') {
        ++pos_;
        return v;
      }
      for (;;) {
        skip();
        if (peek() != '"') fail("expected string key");
        std::string key = str();
        skip();
        if (peek() != ':') fail("expected ':'");
        ++pos_;
        Value child = value(depth + 1);
        v.object.emplace_back(std::move(key), std::move(child));
        skip();
        char d = peek();
        ++pos_;
        if (d == ',') continue;
        if (d == '}') break;
        fail("expected ',' or '}'");
      }
      return v;
    }
    if (c == '[') {
      ++pos_;
      v.type = Type::Array;
      skip();
      if (peek() == ']') {
        ++pos_;
        return v;
      }
      for (;;) {
        v.array.push_back(value(depth + 1));
        skip();
        char d = peek();
        ++pos_;
        if (d == ',') continue;
        if (d == ']') break;
        fail("expected ',' or ']'");
      }
      return v;
    }
    if (c == '"') {
      v.type = Type::String;
      v.string = str();
      return v;
    }
    if (c == 't') {
      expect("true");
      v.type = Type::Bool;
      v.boolean = true;
      return v;
    }
    if (c == 'f') {
      expect("false");
      v.type = Type::Bool;
      return v;
    }
    if (c == 'n') {
      expect("null");
      return v;
    }
    return number();
  }

  Value number() {
    size_t start = pos_;
    bool isFloat = false;
    if (peek() == '-') ++pos_;
    if (pos_ >= s_.size() || !(s_[pos_] >= '0' && s_[pos_] <= '9')) fail("bad number");
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    if (pos_ < s_.size() && s_[pos_] == '.') {
      isFloat = true;
      ++pos_;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      isFloat = true;
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    }
    std::string text = s_.substr(start, pos_ - start);
    Value v;
    if (isFloat) {
      v.type = Type::Double;
      v.number = std::strtod(text.c_str(), nullptr);
    } else {
      v.type = Type::Int;
      v.integer = std::strtoll(text.c_str(), nullptr, 10);
    }
    return v;
  }

  static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  uint32_t hex4() {
    if (pos_ + 4 > s_.size()) fail("bad unicode escape");
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else fail("bad unicode escape");
    }
    return v;
  }

  std::string str() {
    ++pos_;
    std::string out;
    for (;;) {
      if (pos_ >= s_.size()) fail("unterminated string");
      char c = s_[pos_++];
      if (c == '"') break;
      if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= s_.size()) fail("bad escape");
      char e = s_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
              pos_ += 2;
              uint32_t lo = hex4();
              if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
              } else {
                cp = 0xFFFD;
              }
            } else {
              cp = 0xFFFD;
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          appendUtf8(out, cp);
          break;
        }
        default: fail("bad escape");
      }
    }
    return out;
  }

  const std::string& s_;
  size_t pos_;
  size_t maxDepth_;
};

}

Value parse(const std::string& text, size_t maxDepth) {
  Parser p(text, maxDepth);
  return p.run();
}

}
}
