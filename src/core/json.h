#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace mfdp {
namespace json {

enum class Type { Null, Bool, Int, Double, String, Array, Object };

struct Value {
  Type type = Type::Null;
  bool boolean = false;
  int64_t integer = 0;
  double number = 0;
  std::string string;
  std::vector<Value> array;
  std::vector<std::pair<std::string, Value>> object;

  const Value* find(const std::string& key) const;
  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
  bool isString() const { return type == Type::String; }
  bool isInt() const { return type == Type::Int; }
  bool isBool() const { return type == Type::Bool; }
};

Value parse(const std::string& text, size_t maxDepth = 256);

}
}
